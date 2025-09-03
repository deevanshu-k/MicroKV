#ifndef C_STATE_H
#define C_STATE_H

#include <unordered_map>
#include <string>
#include <array>
#include <mutex>
#include <netinet/in.h>

struct State {
    std::unordered_map<std::string, std::string> mp;
    std::mutex m;
};

struct Conn {
    int fd{-1};
    std::string inbuf;
    sockaddr_in peer{};
    std::array<char, 64> ip;
};

struct ConnectionState {
    std::unordered_map<int, Conn> clients;
    std::mutex m;
};

#endif
