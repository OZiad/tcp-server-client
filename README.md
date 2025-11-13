# tcp-server-client
A simple client–server networking project built using concepts from Beej’s Guide to Network Programming written in POSIX-style C++.

This project demonstrates the following of networking fundementals in C++:
- Socket creation (`socket`, `bind`, `listen`, `accept`, `connect`)
- Address resolution via `getaddrinfo`
- Reliable data transfer with `send` and `recv`
- Graceful connection handling and cleanup
- Multithreading for concurrent clients

This repository contains three progressively more advanced TCP networking implementations written in C++. Each version demonstrates a different concurrency and I/O strategy while sharing a similar message format (length-prefixed TCP messages or raw text for the poll version).

### Included versions
##### 1. Basic Blocking Version (`basic-blocking/`)

* Uses plain POSIX sockets (socket, bind, listen, accept, recv, send)
* The server handles one client at a time
* Easiest to understand; good baseline for learning

##### 2. Poll-Based Version (`poll-based/`)

* Single-threaded server using poll()
* Handles multiple clients concurrently in one event loop
* Demonstrates non-blocking multiplexed I/O
* Matches what real chat servers look like conceptually (messages are shared across all users)
* Client is optional; you can use telnet to test it (run server then in a seperate terminal run `telnet localhost 9034`)

##### 3. Multithreaded Version (`multi-threaded/`)

* Thread-per-connection design using std::thread
* Supports bidirectional, length-prefixed messages
