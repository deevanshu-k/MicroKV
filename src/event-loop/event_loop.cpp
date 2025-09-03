#include "event_loop.h"
#include <sys/epoll.h>
#include <unistd.h>

Eventloop::Eventloop() {
    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1");
    }
}

Eventloop::~Eventloop() {
    close(epfd);
}

auto Eventloop::add(FileDescriptor fd, Handler h) -> bool {
    epoll_event event{};
    event.data.fd = fd.fd;
    event.events = fd.events;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd.fd, &event) < 0) {
        perror("epoll_ctl add");
        return false;
    }

    handlers[fd.fd] = std::move(h);

    return true;
}

auto Eventloop::mod(FileDescriptor fd) -> bool {
    epoll_event event{};
    event.data.fd = fd.fd;
    event.events = fd.events;

    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd.fd, &event) < 0) {
        perror("epoll_ctl mod");
        return false;
    }

    return true;
}

auto Eventloop::del(int fd) -> bool {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    return true;
}

auto Eventloop::run() -> void {
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
