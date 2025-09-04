# C++ Mini Server

A low-level TCP mini server written in C++20.
Learning project to explore systems + networking.

## Build
```bash
mkdir build && cd build
cmake ..
make
./server

## Dev Command
`rm build/server  && clang++ -std=c++20 src/main.cpp src/utils/utils.cpp src/event-loop/event_loop.cpp src/command/tcp_server.cpp -o build/server && ./build/server`
