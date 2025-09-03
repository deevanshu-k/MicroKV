#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "utils/utils.h"
#include "event-loop/event_loop.h"

struct State {
    std::unordered_map<std::string, std::string> mp;
    std::mutex m;
};

static auto set_nonblock(int fd) -> bool {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

auto main() -> int {
    std::cout<< "C++, The language of God\n";

    /*
     * Telling program to ignore the sigpipe signal
     * Which come when program have broken pipe
     * It will just return the error
     */
    signal(SIGPIPE, SIG_IGN);

    /*
     * socket() creates an endpoint for communication — like opening a door for network data.
     * It returns a file descriptor (an integer) that your program will use to read/write network data.
     * Think of it as your program getting a handle (address) to talk over the network using rules you specify (IPv4, TCP/UDP).
     */
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket");
    }

    /*
     * when the program cloosed, the os sometimes hold the address and port for a while
     * so when program restarts, the bind fail
     * we use this to allow quick restart
     */
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(socket_fd);
        return 1;
    }

    /*
     * Bind to 0.0.0.0:5000
     */
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // host to network long
    addr.sin_port = htons(5000); // host to network short

    if (bind(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        close(socket_fd);
        return 1;
    }

    if (!set_nonblock(socket_fd))
    {
        std::cerr << "failed to set nonblock\n";
        return 1;
    }

    /*
     * Listen
     */
    if (listen(socket_fd, 128) < 0) {
        perror("listen");
        close(socket_fd);
        return 1;
    }

    std::cout << "MicroKV server listening on 0.0.0.0:5000\n";

    /*
     * In-Memory Storage
     */
    auto* kv = new State();

    /*
     * Connection Map
     */
     std::unordered_map<int, Conn> clients;
     std::mutex m;

    /*
     * Event Loop
     */
    Eventloop loop;

    auto tcp_socket_fd = FileDescriptor{};
    tcp_socket_fd.fd = socket_fd;
    tcp_socket_fd.events = EPOLLIN;

    loop.add(tcp_socket_fd, [&](uint32_t ev) {
        if (!(ev & EPOLLIN)) return;

        while (true) {
            /*
            * Accept a client (blocking)
            */
            sockaddr_in cli{};
            socklen_t clilen = sizeof(cli);
            int cfd = accept4(socket_fd, reinterpret_cast<sockaddr*>(&cli), &clilen, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; // drained
                if (errno == EINTR) continue;
                perror("accept4");
                break;
            }

            /*
             * Handle client
             */
            Conn c{};
            c.fd = cfd;
            c.peer = cli;

            /*
            * Read client address
            */
            inet_ntop(AF_INET, &cli.sin_addr, c.ip.begin(), sizeof(c.ip));
            std::cout << "Client connected from " << c.ip.data() << ":" << ntohs(cli.sin_port) << "\n";

            clients[cfd] = c;
            auto client_socket_fd = FileDescriptor {};
            client_socket_fd.fd = cfd;
            client_socket_fd.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
            loop.add(client_socket_fd, [&](uint32_t events){
                auto it = clients.find(cfd);
                if (it == clients.end()) {
                    return;
                }
                Conn& client = it->second;

                if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    std::cout << "Client disconnected " << client.ip.data() << ":" << ntohs(client.peer.sin_port) << "\n";
                    loop.del(client.fd);
                    clients.erase(client.fd);
                    return;
                }

                if (events & EPOLLIN) {
                    // Read data until EAGAIN
                    std::array<char, 1024> buf;
                    while(true) {
                        ssize_t n = read(client.fd, buf.begin(), sizeof(buf));

                        if (n > 0) {
                            client.inbuf.append(buf.data(), n);

                            // process completed lines
                            size_t pos;
                            while ((pos = client.inbuf.find('\n')) != std::string::npos) {
                                auto line = client.inbuf.substr(0, pos);
                                client.inbuf.erase(0, pos + 1);

                                std::cout << "[" << client.fd << "]" << " " << line << "\n";

                                auto command = split(line, ' ');

                                std::string response = "Wrong command";
                                if (command.size() > 0) {
                                    if (command[0] == "GET" && command.size() == 2) {
                                        kv->m.lock();
                                        if (kv->mp.find(command[1]) != kv->mp.end()) {
                                            response = kv->mp[command[1]];
                                        } else {
                                            response = "Key not found";
                                        }
                                        kv->m.unlock();
                                    }
                                    if (command[0] == "POST" && command.size() == 3) {
                                        kv->m.lock();
                                        kv->mp[command[1]] = command[2];
                                        response = "OK";
                                        kv->m.unlock();
                                    }
                                }
                                response += "\n";
                                write(client.fd, response.data(), response.size());
                            }
                        } else if (n == 0) {
                            // client closed
                            loop.del(client.fd);
                            clients.erase(client.fd);
                            return;
                        } else {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            if (errno == EINTR) continue;
                            perror("read");
                            loop.del(client.fd);
                            clients.erase(it);
                            return;
                        }
                    }
                }
            });
        }
    });

    loop.run();

    return 0;
}
