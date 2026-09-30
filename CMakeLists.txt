cmake_minimum_required(VERSION 3.16)
project(multithreaded_http_server VERSION 0.1.0 LANGUAGES CXX)

if(NOT UNIX)
    message(FATAL_ERROR "This project requires POSIX sockets. On Windows, build inside Linux/WSL.")
endif()

add_executable(http_server
    src/logger.cpp
    src/shutdown_signal.cpp
    src/main.cpp
    src/tcp_server.cpp
    src/client_task_queue.cpp
    src/http_request.cpp
    src/lru_cache.cpp
    src/static_files.cpp
    src/http_router.cpp
    src/http_response.cpp
)
target_include_directories(http_server PRIVATE include)
find_package(Threads REQUIRED)
target_link_libraries(http_server PRIVATE Threads::Threads)
target_compile_features(http_server PRIVATE cxx_std_17)
set_target_properties(http_server PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(http_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(lru_cache_test tests/lru_cache_test.cpp src/lru_cache.cpp)
    target_include_directories(lru_cache_test PRIVATE include)
    target_compile_features(lru_cache_test PRIVATE cxx_std_17)
    set_target_properties(lru_cache_test PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(lru_cache_test PRIVATE Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(lru_cache_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME lru_cache COMMAND lru_cache_test)
    set_tests_properties(lru_cache PROPERTIES TIMEOUT 10)
    add_executable(client_task_queue_test tests/client_task_queue_test.cpp src/client_task_queue.cpp)
    target_include_directories(client_task_queue_test PRIVATE include)
    target_compile_features(client_task_queue_test PRIVATE cxx_std_17)
    set_target_properties(client_task_queue_test PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(client_task_queue_test PRIVATE Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(client_task_queue_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME client_task_queue COMMAND client_task_queue_test)
    set_tests_properties(client_task_queue PROPERTIES TIMEOUT 10)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME tcp_smoke
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp_smoke.py
                    $<TARGET_FILE:http_server>)
        add_test(NAME load_metrics
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/load_test.py)
        add_test(NAME load_regression
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark.py
                    $<TARGET_FILE:http_server> --requests 24)
        set_tests_properties(load_metrics PROPERTIES TIMEOUT 10)
        set_tests_properties(load_regression PROPERTIES TIMEOUT 30)
        add_test(NAME shutdown
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/shutdown.py
                    $<TARGET_FILE:http_server>)
        set_tests_properties(shutdown PROPERTIES TIMEOUT 20)
        add_test(NAME static_files
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/static_files.py
                    $<TARGET_FILE:http_server>)
        set_tests_properties(static_files PROPERTIES TIMEOUT 20)
        set_tests_properties(tcp_smoke PROPERTIES TIMEOUT 20
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()
