# C++11 Standard High Performance WebServer

This project is a high-performance web server based on the C++11 standard, implemented using the Reactor pattern. The server processes HTTP requests using a multi-threaded approach, supporting static resource access and dynamic page generation, with high concurrency, low latency, and high stability. Additionally, the server provides a variety of configuration options and performance optimization strategies, allowing for flexible adjustments based on actual needs.

## Key Features

- HTTP protocol parsing: Supports HTTP/1.1 protocol parsing, including request lines, headers, and bodies.
- Multi-threaded processing: Uses a multi-threaded approach to handle HTTP requests, improving concurrency.
- Static resource access: Supports access to static resources, including HTML, CSS, JavaScript, and images.
- Dynamic page generation: Supports dynamic page generation, implemented using CGI.
- Configuration options: Provides a variety of configuration options, including port numbers, thread counts, log levels, etc.
- Performance optimization: Provides a variety of performance optimization strategies, including TCP_NODELAY, SO_REUSEADDR, EPOLLET, etc.

## Technology Stack

- C++11 standard
- Reactor pattern
- Multi-threaded programming
- Network programming
- CGI programming

## System Architecture



The system implements the Reactor pattern, including one main thread and multiple worker threads. The main thread is responsible for listening and accepting network connections, and assigns connections to worker threads for processing. The worker threads implement concurrent processing using multiplexing techniques, including parsing HTTP requests, accessing static resources, generating dynamic pages, etc. The system is written in C++11, taking full advantage of the new features of the C++11 standard to improve code readability and maintainability.

## Performance Testing

The system has undergone multiple performance tests and has high concurrency and stability. The test environment is CentOS 7.0 x64, Intel(R) Xeon(R) CPU E5-2620 v2 @ 2.10GHz, 16GB memory, and a 1Gbps network card.

The test results are as follows:

- Static page request: QPS can reach about 100,000, with response times under 1ms.
- Dynamic page request: QPS can reach about 50,000, with response times under 5ms.