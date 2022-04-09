add_library(femu STATIC femu.c ftl.c)
target_link_libraries(femu pthread)
