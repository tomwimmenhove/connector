cmake_minimum_required(VERSION 2.8.9)
project (connector)
add_executable(connector connector.cpp telnet.cpp main.cpp conn_pool.cpp)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

