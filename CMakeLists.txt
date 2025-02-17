cmake_minimum_required(VERSION 3.10)
project(http_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(http_server
    main.cpp
    http_server.cpp
)

target_link_libraries(http_server pthread) 