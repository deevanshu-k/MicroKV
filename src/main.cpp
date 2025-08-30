#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

    while (true) {
        /*
         * Accept a client (blocking)
         */
        sockaddr_in cli{};
        socklen_t clilen = sizeof(cli);
        int cfd = accept(socket_fd, reinterpret_cast<sockaddr*>(&cli), &clilen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        /*
         * Read client address
         */
        std::array<char, 64> ip{};
        inet_ntop(AF_INET, &cli.sin_addr, ip.begin(), sizeof(ip));
        std::cout << "Client connected from " << ip.begin() << ":" << ntohs(cli.sin_port) << "\n";

        /*
         * Create data buffer
         */
        std::array<char, 1024> buffer;
        std::array<char, 1024> wbuffer;
        std::string data;
        while (true) {
            /*
             * Read data
             */
            ssize_t bytes_read = read(cfd, buffer.begin(), sizeof(buffer) - 1);
            if (bytes_read <= 0) {
                std::cout << "Client disconnected " << ip.begin() << ":" << ntohs(cli.sin_port) << " \n";
                break;
            }

            /*
             * Add end of string
             */
            buffer[bytes_read] = '\0';
            data += buffer.begin();

            /*
             * Fetch line
             */
            size_t pos;
            while ((pos = data.find('\n')) != std::string::npos) {
                std::string line = data.substr(0, pos);
                data.erase(0, pos+1);
                std::cout << "[" << cfd << "] > Data: " << line << "\n";
            }

            /*
             * Send response
             */
            std::strcpy(wbuffer.data(), "OK \n");
            size_t wbufflen = std::strlen(wbuffer.data());
            ssize_t bytes_write = write(cfd, wbuffer.data(), wbufflen);
        }
    }

    return 0;
}
