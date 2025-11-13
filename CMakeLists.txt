cmake_minimum_required(VERSION 4.0)
project(tcp_server_client)

set(CMAKE_CXX_STANDARD 20)

add_executable(tcp_server_client
        basic-blocking/server.cpp
        basic-blocking/client.cpp
        poll-based/pollServer.cpp
        multi-threaded/multithreadServer.cpp
        multi-threaded/multithreadClient.cpp
)
