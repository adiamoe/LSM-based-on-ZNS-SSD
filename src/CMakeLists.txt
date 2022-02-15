include_directories(include)

add_subdirectory(femu)

add_executable(main main.c)
target_link_libraries(main femu)
