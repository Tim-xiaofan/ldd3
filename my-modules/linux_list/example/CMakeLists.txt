project(example)

cmake_minimum_required(VERSION 3.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(APP rw)
set(SRC main.cpp)

add_executable(${APP} ${SRC})
target_link_libraries(${APP} pthread)
set_source_files_properties(${SRC} PROPERTIES COMPILE_FLAGS -O2 -Wall -Werror)
