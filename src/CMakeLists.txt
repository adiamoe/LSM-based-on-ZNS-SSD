include_directories(include LSM-Tree)

add_subdirectory(femu)
add_subdirectory(LSM-Tree)

add_executable(ZNS-SSD main.cpp test.h include/utils.cpp readThreadPool.h)
target_link_libraries(ZNS-SSD LSM-Tree femu)
