# tcp-server-client
A simple client–server networking project built using concepts from Beej’s Guide to Network Programming written in POSIX-style C++.
The goal is to understand how TCP sockets work and extend it into a working multiclient chat or data exchange system.

This project demonstrates the following of networking fundementals in C++:
- Socket creation (`socket`, `bind`, `listen`, `accept`, `connect`)
- Address resolution via `getaddrinfo`
- Reliable data transfer with `send` and `recv`
- Graceful connection handling and cleanup
- Multithreading for concurrent clients

