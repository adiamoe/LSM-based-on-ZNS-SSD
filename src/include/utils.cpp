//
// Created by ZJW on 2022/3/19.
//

#include "utils.h"

void MemoryManager::getValue(int level, int num, uint64_t offset, uint64_t length) {
    if(completeTable[level].find(num) == completeTable[level].end() || completeTable[level][num].get() == 0) {
        completeTable[level].erase(num);
        start s = mappingTable[level][num];
        uint32_t left = (s + offset) / DEFAULT_PAGE_SIZE;  //对齐页边界
        uint32_t right = (s + offset + length) / DEFAULT_PAGE_SIZE;
        uint32_t batch_left = UINT32_MAX, batch_right = UINT32_MAX;
        vector<pair<int32_t, int32_t>> interval;
        uint32_t page = left;
        //对已经缓存的页，直接跳过
        for (; page <= right; ++page) {
            if (pageCache.get(page) == -1)
                break;
        }
        //遇到没有缓存的页，记录区间并缓存
        for (; page <= right; ++page) {
            if (pageCache.get(page) == -1) {
                if (batch_left == UINT32_MAX)
                    batch_left = page;
                batch_right = page;
                pageCache.put(page);
            } else if (batch_left != UINT32_MAX) {
                interval.emplace_back(batch_left, batch_right);
                batch_left = UINT32_MAX;
            }
        }
        if (batch_left != UINT32_MAX)
            interval.emplace_back(batch_left, batch_right);

        //读缓存的区间
        for (auto &it: interval) {
            uint64_t len = (it.second - it.first + 1) * DEFAULT_PAGE_SIZE;
            uint64_t oft = it.first * DEFAULT_PAGE_SIZE;
            femu_read(femu, len, oft, nullptr);
        }
    }
}

void MemoryManager::deleteTable(int level, int num) {
    assert(completeTable[level].find(num) == completeTable[level].end());
    if(mappingTable[level].find(num) == mappingTable[level].end())
        return;
    start s = mappingTable[level][num];
    mappingTable[level].erase(num);
    remappingTable.erase(s);

    int order = s / (SSD_SIZE / zone_num);
    zone_metas[order].valid_table_num--;
    zone_metas[order].deleted_table.emplace_back(s);
    resetZone(order);

    uint32_t left = s / DEFAULT_PAGE_SIZE;  //对齐页边界
    uint32_t right = (s + DEFAULT_SSTABLE_SIZE) / DEFAULT_PAGE_SIZE;
    for(uint32_t page = left; page <= right; ++page) {
        pageCache.del(page);
    }
}

void MemoryManager::readTable(int level, int num, uint64_t offset, uint64_t length) {
    if(completeTable[level].find(num) == completeTable[level].end() || completeTable[level][num].get() == 0) {
        completeTable[level].erase(num);
        start s = mappingTable[level][num];
        uint32_t left = (s + offset) / DEFAULT_PAGE_SIZE * DEFAULT_PAGE_SIZE;  //对齐页边界
        uint32_t right = ((s + offset + length) / DEFAULT_PAGE_SIZE + 1)* DEFAULT_PAGE_SIZE;
        femu_read(femu,right-left, left, nullptr);
    }
}

void MemoryManager::writeTable(int level, int num) {
    if(empty_zone.size() < 30)
        printf("empty_zone.size() = %u\n", empty_zone.size());
    start s = zone_metas[open_zone[which_zone]].write_ptr;
    zone_meta &meta = zone_metas[open_zone[which_zone]];
    meta.write_ptr += DEFAULT_SSTABLE_SIZE;
    meta.valid_table_num++;
    if(meta.write_ptr >= meta.end) {
        full_zone.emplace(open_zone[which_zone]);
        open_zone[which_zone] = empty_zone.front();
        empty_zone.pop();
    }
    which_zone = (++which_zone) % 10;
    if(mappingTable.size()<level+1) {
        mappingTable.emplace_back();
        completeTable.emplace_back();
    }
    mappingTable[level][num] = s;
    remappingTable[s] = {level, num};
    //femu_write(femu, DEFAULT_SSTABLE_SIZE, s, nullptr);
    completeTable[level][num] = pool.enqueue(which_zone, &femu_write, femu, DEFAULT_SSTABLE_SIZE, s, nullptr);
}

void MemoryManager::resetZone(int zone_order) {
    auto &meta = zone_metas[zone_order];

    //只清空写满的zone
    if(meta.write_ptr < meta.end)
        return;

    if(full_zone.find(zone_order) == full_zone.end()) {
        printf("Error: The full zone is not in the full queue\n");
    }

    unsigned maximum_table = (SSD_SIZE / zone_num) / DEFAULT_SSTABLE_SIZE;
    double rate = (double) meta.valid_table_num / maximum_table;

    //利用率不足0.3时清空整个zone
    if(rate < 0.3) {
        uint64_t zone_size = SSD_SIZE / zone_num;
        uint64_t start_table = meta.start;
        uint64_t end_table = meta.end;
        vector<uint64_t> rewrite_table;
        //先排序，降低搜索的时间复杂度
        std::sort(meta.deleted_table.begin(), meta.deleted_table.end());
        for(uint64_t table = start_table, index = 0; table < end_table; table += DEFAULT_SSTABLE_SIZE) {
            if(meta.deleted_table[index] == table) {
                index++;
            } else {
                femu_read(femu, DEFAULT_SSTABLE_SIZE, table, nullptr);
                rewrite_table.emplace_back(table);
            }
        }
        unsigned i = 0, prev = 0;
        uint64_t table;
        while(i < rewrite_table.size()) {
            unsigned to_zone = empty_zone.front();
            empty_zone.pop();

            start s = zone_metas[to_zone].write_ptr;
            zone_meta &to_meta = zone_metas[to_zone];
            for (table = to_meta.write_ptr; table < to_meta.end && i < rewrite_table.size();
                 table += DEFAULT_SSTABLE_SIZE, i++) {
                auto it = remappingTable[rewrite_table[i]];
                remappingTable.erase(rewrite_table[i]);
                mappingTable[it.first][it.second] = table;
                remappingTable[table] = it;
            }
            to_meta.write_ptr += (i - prev) * DEFAULT_SSTABLE_SIZE;
            to_meta.valid_table_num += i - prev;
            femu_write(femu, (i - prev) * DEFAULT_SSTABLE_SIZE, s, nullptr);
            if(to_meta.write_ptr >= to_meta.end) {
                full_zone.emplace(to_zone);
                prev = i;
            } else {
                empty_zone.emplace(to_zone);
            }
        }
        femu_reset(femu, zone_size, meta.start, nullptr);
        meta.write_ptr = meta.start;
        meta.valid_table_num = 0;
        meta.deleted_table.clear();
        empty_zone.emplace(zone_order);
        full_zone.erase(zone_order);
    }
}