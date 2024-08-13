project(process-vm)

cmake_minimum_required(VERSION 3.0)

set(APP procss_vm)
set(SRC main.c)

add_executable(${APP} ${SRC})
set_source_files_properties(${SRC} PROPERTIES COMPILE_FLAGS -O2 -Wall -Werror)
