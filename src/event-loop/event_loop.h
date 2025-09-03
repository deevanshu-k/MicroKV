#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <cstdint>
#include <string>
#include <array>
#include <functional>
#include "netinet/in.h"

struct FileDescriptor {
    int fd;
    uint32_t events;
};

struct Conn {
    int fd{-1};
    std::string inbuf;
    sockaddr_in peer{};
    std::array<char, 64> ip;
};

class Eventloop {
public:
    using Handler = std::function<void(uint32_t events)>;

    Eventloop();

    ~Eventloop();

    auto add(FileDescriptor fd, Handler h) -> bool;

    auto mod(FileDescriptor fd) -> bool;

    auto del(int fd) -> bool;

    auto run() -> void;

private:
    int epfd{-1};
    std::unordered_map<int, Handler> handlers;
};

#endif
