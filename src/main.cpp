#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "event-loop/event_loop.h"
#include "command/command.h"
#include "state/state.h"

auto main() -> int {
    std::cout<< "C++, The language of God\n";

    /*
     * Telling program to ignore the sigpipe signal
     * Which come when program have broken pipe
     * It will just return the error
     */
    signal(SIGPIPE, SIG_IGN);

    /*
     * In-Memory Storage
     */
    auto kv = new State();

    /*
     * Connection Map
     */
    auto connection_state = new ConnectionState();

    /*
     * Event Loop
     */
    auto loop = new Eventloop();

    add_tcp_command_server(loop, kv, connection_state);

    loop->run();

    return 0;
}
