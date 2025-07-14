cmake_minimum_required(VERSION 3.15)
project(sleepyshell C)

set(CMAKE_C_STANDARD 11)

add_executable(sleepyshell app/main.c
        app/builtins.c
)
