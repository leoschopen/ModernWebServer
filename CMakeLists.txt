cmake_minimum_required(VERSION 3.10)

project(server2)

set(CMAKE_CXX_STANDARD 14)

set(SOURCES
        main.cc
        log/server_log.cc
        log/server_log.h
        log/blockqueue.h
        http/httpconn.cc
        http/httpconn.h
        http/httprequest.cc
        http/httprequest.h
        http/httpresponse.cc
        http/httpresponse.h
        pool/sqlconnpool.cc
        pool/sqlconnpool.h
        pool/sqlconnRAII.h
        pool/threadpool.h
        timer/heaptimer.cc
        timer/heaptimer.h
        buffer/buffer.cc
        buffer/buffer.h
        server/webserver.cc
        server/webserver.h
        server/epoller.cc
        server/epoller.h
        )

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_options(${PROJECT_NAME} PRIVATE -O3 -Wall -g)

target_include_directories(${PROJECT_NAME} PRIVATE
    log
    http
    pool
    timer
    buffer
    server
)

target_link_libraries(${PROJECT_NAME} PRIVATE pthread mysqlclient)