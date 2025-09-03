#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <iostream>
#include"../event-loop/event_loop.h"
#include"../state/state.h"
#include"../utils/utils.h"

static auto set_nonblock(int fd) -> bool {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

auto add_tcp_command_server(Eventloop* loop, State* state, ConnectionState* connection_state) -> int {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt REUSEADDR");
    }

    #ifdef SO_REUSEPORT
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt REUSEPORT");
    }
    #endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(5000);
    if (bind(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind"); close(socket_fd); return 1;
    }

    if (!set_nonblock(socket_fd)) {
        std::cerr << "failed to set nonblock\n";
        close(socket_fd); return 1;
    }

    if (listen(socket_fd, 128) < 0) {
        perror("listen");
        close(socket_fd); return 1;
    }

    std::cout << "MicroKV server listening on 0.0.0.0:5000\n";

    // Make sure the unordered_map has some buckets to start with
    connection_state->clients.reserve(256);

    FileDescriptor tcp_socket_fd{};
    tcp_socket_fd.fd = socket_fd;
    tcp_socket_fd.events = EPOLLIN;

    loop->add(tcp_socket_fd, [loop, state, connection_state, socket_fd](uint32_t ev) {
        if (!(ev & EPOLLIN)) return;

        while (true) {
            sockaddr_in cli{};
            socklen_t clilen = sizeof(cli);
            int cfd = accept4(socket_fd, reinterpret_cast<sockaddr*>(&cli), &clilen, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                perror("accept4");
                break;
            }

            Conn c{};
            c.fd = cfd;
            c.peer = cli;
            inet_ntop(AF_INET, &cli.sin_addr, c.ip.begin(), static_cast<socklen_t>(c.ip.size()));
            std::cout << "Client connected from " << c.ip.data() << ":" << ntohs(cli.sin_port) << "\n";

            {
                std::lock_guard<std::mutex> lg(connection_state->m);
                connection_state->clients.emplace(cfd, c);
            }

            FileDescriptor client_socket_fd{};
            client_socket_fd.fd = cfd;
            client_socket_fd.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;

            // capture client_fd by value to avoid referencing the changing local 'cfd'
            int client_fd = cfd;
            loop->add(client_socket_fd, [loop, state, connection_state, client_fd](uint32_t events) {
                // Get a snapshot of client info (copy) under lock
                Conn clientSnapshot;
                {
                    std::lock_guard<std::mutex> lg(connection_state->m);
                    auto it = connection_state->clients.find(client_fd);
                    if (it == connection_state->clients.end()) {
                        return; // already removed
                    }
                    clientSnapshot = it->second;
                }

                if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    std::cout << "Client disconnected " << clientSnapshot.ip.data()
                              << ":" << ntohs(clientSnapshot.peer.sin_port) << "\n";

                    loop->del(client_fd);
                    close(client_fd);

                    std::lock_guard<std::mutex> lg(connection_state->m);
                    connection_state->clients.erase(client_fd);
                    return;
                }

                if (events & EPOLLIN) {
                    std::array<char, 1024> buf;
                    while (true) {
                        ssize_t n = read(client_fd, buf.data(), buf.size());
                        if (n > 0) {
                            // Append to the real connection's buffer under lock and process lines there
                            std::vector<std::string> responses;
                            {
                                std::lock_guard<std::mutex> lg(connection_state->m);
                                auto &ref = connection_state->clients[client_fd]; // safe while locked
                                ref.inbuf.append(buf.data(), static_cast<size_t>(n));

                                size_t pos;
                                while ((pos = ref.inbuf.find('\n')) != std::string::npos) {
                                    std::string line = ref.inbuf.substr(0, pos);
                                    ref.inbuf.erase(0, pos + 1);
                                    std::cout << "[" << ref.fd << "] " << line << "\n";

                                    auto command = split(line, ' ');
                                    std::string response = "Wrong command";
                                    if (!command.empty()) {
                                        if (command[0] == "GET" && command.size() == 2) {
                                            std::lock_guard<std::mutex> sg(state->m);
                                            auto it = state->mp.find(command[1]);
                                            response = (it != state->mp.end()) ? it->second : "Key not found";
                                        } else if (command[0] == "POST" && command.size() == 3) {
                                            std::lock_guard<std::mutex> sg(state->m);
                                            state->mp[command[1]] = command[2];
                                            response = "OK";
                                        }
                                    }
                                    response.push_back('\n');
                                    responses.push_back(std::move(response)); // collect to write after unlock
                                }
                            } // unlock before writes

                            // write all responses (do not hold connection_state lock while writing)
                            for (auto &resp : responses) {
                                ssize_t wn = write(client_fd, resp.data(), resp.size());
                                (void)wn;
                            }
                        } else if (n == 0) {
                            // client closed cleanly
                            loop->del(client_fd);
                            close(client_fd);
                            std::lock_guard<std::mutex> lg(connection_state->m);
                            connection_state->clients.erase(client_fd);
                            return;
                        } else {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            if (errno == EINTR) continue;
                            perror("read");
                            loop->del(client_fd);
                            close(client_fd);
                            std::lock_guard<std::mutex> lg(connection_state->m);
                            connection_state->clients.erase(client_fd);
                            return;
                        }
                    } // while read
                } // if EPOLLIN
            }); // client handler lambda
        } // accept while
    }); // listener lambda

    return 0;
}
