cmake_minimum_required(VERSION 3.10)

project(server2)

set(CMAKE_CXX_STANDARD 11)

set(SOURCES
        main.cc
        log/server_log.cc
        log/server_log.h
        log/block_queue.h
        lock/locker.h
        http/http_connection.cc
        http/http_connection.h
        threadpool/threadpool.h
        utils/server_utils.cc
        utils/server_utils.h
        utils/socket_utils.h
        CGImysql/sql_connection_pool.h
        CGImysql/sql_connection_pool.cc
        timer/server_timer.cc
        timer/server_timer.h)
add_executable(server2 ${SOURCES})

target_link_libraries(server2 pthread mysqlclient)