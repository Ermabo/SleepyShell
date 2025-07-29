cmake_minimum_required(VERSION 3.10)

project(sleepyshell C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(sleepyshell
        src/main.c
        src/builtins.c
        src/path_utils.c
        src/tokenizer.c
)

add_executable(tokenizer_test
        test/tokenizer_test.c
        src/tokenizer.c
)

enable_testing()

add_test(NAME TokenizerTest COMMAND tokenizer_test)