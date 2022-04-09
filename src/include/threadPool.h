//
// Created by ZJW on 2022/3/16.
//

#ifndef FEMU_SIM_THREADPOOL_H
#define FEMU_SIM_THREADPOOL_H

#include <memory>
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <pthread.h>
#include <iostream>

class ThreadPool {
public:
    explicit ThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(int index, F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue
    std::vector<std::queue<std::function<void()>>> tasks;

    // synchronization
    std::vector<std::mutex> queue_mutex;
    std::vector<std::condition_variable> condition;
    std::atomic<bool> stop;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
        :stop(false), tasks(threads), queue_mutex(threads), condition(threads)
{
    for(size_t i = 0;i<threads;++i) {
        workers.emplace_back(
                [this, i] {
                    while (true) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex[i]);
                            this->condition[i].wait(lock,
                                                    [this, i] { return stop.load() || !(tasks[i].empty()); });
                            if (stop.load() && tasks[i].empty())
                                return;
                            task = std::move(tasks[i].front());
                            this->tasks[i].pop();
                        }

                        task();
                    }
                }
        );

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i,&cpuset);

        int rc = pthread_setaffinity_np(workers[i].native_handle(),sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
        }
    }
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(int index, F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::lock_guard<std::mutex> lock(queue_mutex[index]);

        // don't allow enqueueing after stopping the pool
        if(stop.load())
            std::cerr<<"KVStore has been closed"<<std::endl;

        tasks[index].emplace([task](){ (*task)(); });
    }
    condition[index].notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    stop.store(true);
    for(auto &con:condition) {
        con.notify_all();
    }
    for(std::thread &worker: workers)
        worker.join();
}

#endif //FEMU_SIM_THREADPOOL_H
