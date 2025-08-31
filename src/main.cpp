#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>

static bool set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

class Eventloop {
public:
    using Handler = std::function<void(uint32_t events)>;

    Eventloop() {
        epfd = epoll_create1(EPOLL_CLOEXEC);
        if (epfd < 0) {
            perror("epoll_create1");
        }
    }

    ~Eventloop() {
        close(epfd);
    }

    auto add(int fd, uint32_t events, Handler h) -> bool {
        epoll_event event{};
        event.data.fd = fd;
        event.events = events;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) < 0) {
            perror("epoll_ctl add");
            return false;
        }

        handlers[fd] = std::move(h);

        return true;
    }

    auto mod(int fd, uint32_t events) -> bool {
        epoll_event event{};
        event.data.fd = fd;
        event.events = events;

        if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event) < 0) {
            perror("epoll_ctl mod");
            return false;
        }

        return true;
    }

    auto del(int fd) -> bool {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        return true;
    }

    auto run() -> void {
        std::vector<epoll_event> events(128);
        while (true) {
            int n = epoll_wait(epfd, events.data(), (int)events.size(), -1);
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("epoll_wait");
                break;
            }

            for (int i(0); i < n; i++) {
                int fd = events[i].data.fd;
                auto it = handlers.find(fd);
                if (it != handlers.end()) {
                    it->second(events[i].events);
                }
            }

            if (n == (int)events.size()) events.resize(events.size() * 2);
        }

    }

private:
    int epfd{-1};
    std::unordered_map<int, Handler> handlers;
};

struct Conn {
    int fd{-1};
    std::string inbuf;
    sockaddr_in peer{};
    std::array<char, 64> ip;
};

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
    std::map<std::string, std::string> kv;

    /*
     * Connection Map
     */
     std::unordered_map<int, Conn> clients;

    /*
     * Event Loop
     */
    Eventloop loop;

    loop.add(socket_fd, EPOLLIN, [&](uint32_t ev) {
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

            loop.add(cfd, EPOLLIN | EPOLLRDHUP | EPOLLERR, [&](uint32_t events){
                auto it = clients.find(cfd);
                if (it == clients.end()) {
                    return;
                }
                Conn& client = it->second;

                if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    std::cout << "client disconnected " << client.ip.data() << ":" << ntohs(client.peer.sin_port) << "\n";
                    loop.del(client.fd);
                    clients.erase(client.fd);
                    return;
                }

                if (events & EPOLLIN) {
                    // Read data until EAGAIN
                }
            });
        }
    });

    loop.run();

    return 0;
}
