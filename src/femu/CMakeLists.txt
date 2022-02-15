add_library(femu STATIC femu.c ftl.c pqueue.c rte_ring.c)
target_link_libraries(femu pthread)
