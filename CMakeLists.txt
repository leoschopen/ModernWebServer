cmake_minimum_required(VERSION 3.10)

project(server2)

set(CMAKE_CXX_STANDARD 14)

set(SOURCES
        main.cc
        log/server_log.cc
        log/server_log.h
        log/blockqueue.h
        lock/locker.h
        http/httpconn.cc
        http/httpconn.h
        http/httprequest.cc
        http/httprequest.h
        http/httpresponse.cc
        http/httpresponse.h
        utils/server_utils.cc
        utils/server_utils.h
        utils/socket_utils.h
        pool/sqlconnpool.cc
        pool/sqlconnpool.h
        pool/sqlconnRAII.h
        pool/threadpool.h
        CGImysql/sql_connection_pool.h
        CGImysql/sql_connection_pool.cc
        timer/heaptimer.cc
        timer/heaptimer.h
        timer/timer.cc
        timer/timer.h
        buffer/buffer.cc
        buffer/buffer.h
        server/webserver.cc
        server/webserver.h
        server/epoller.cc
        server/epoller.h
        )
add_executable(server2 ${SOURCES})

target_link_libraries(server2 pthread mysqlclient)