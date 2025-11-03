cmake_minimum_required(VERSION 3.20)
project(ConcurrentKVS VERSION 1.0.0 LANGUAGES CXX)

# Compiler settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address -fsanitize=thread")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Library interface (header-only for templates)
add_library(kvs_lib INTERFACE)
target_include_directories(kvs_lib INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Server executable
add_executable(kvs_server
    src/apps/server/main.cpp
    src/net/server.cpp
)
target_link_libraries(kvs_server 
    PRIVATE kvs_lib
    PRIVATE pthread
    PRIVATE rt  # For shared memory functions
)

# Client executable  
add_executable(kvs_client
    src/apps/client/main.cpp
    src/net/client.cpp
)
target_link_libraries(kvs_client
    PRIVATE kvs_lib
    PRIVATE pthread
    PRIVATE rt
)

# Test executable for Phase 3 verification
add_executable(test_operations
    tests/test_operations.cpp
)
target_link_libraries(test_operations
    PRIVATE kvs_lib
    PRIVATE pthread
)

# Installation rules
install(TARGETS kvs_server kvs_client
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include/concurrent-kvs
    FILES_MATCHING PATTERN "*.hpp"
)

# Print configuration summary
message(STATUS "")
message(STATUS "Concurrent KVS Configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
