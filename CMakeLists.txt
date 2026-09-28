cmake_minimum_required(VERSION 3.16)
project(multithreaded_http_server VERSION 0.1.0 LANGUAGES CXX)

if(NOT UNIX)
    message(FATAL_ERROR "This project requires POSIX sockets. On Windows, build inside Linux/WSL.")
endif()

add_executable(http_server
    src/main.cpp
    src/tcp_server.cpp
    src/http_request.cpp
    src/http_response.cpp
)
target_include_directories(http_server PRIVATE include)
target_compile_features(http_server PRIVATE cxx_std_17)
set_target_properties(http_server PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(http_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME tcp_smoke
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp_smoke.py
                    $<TARGET_FILE:http_server>)
        set_tests_properties(tcp_smoke PROPERTIES TIMEOUT 20)
    endif()
endif()
