#ifndef C_SOCKET_H
#define C_SOCKET_H

#include"../event-loop/event_loop.h"
#include"../state/state.h"

auto add_tcp_command_server(Eventloop* loop, State* state, ConnectionState* connection_state) -> int;

#endif
