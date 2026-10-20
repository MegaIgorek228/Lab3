cmake_minimum_required(VERSION 3.20)
project(Lab3 C)

set(CMAKE_C_STANDARD 11)

find_package(Threads REQUIRED)

add_executable(main main.c
        main.c
        timsort.c
        timsort.h)

target_link_libraries(main PRIVATE Threads::Threads)