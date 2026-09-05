#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
    guest_port = 18001,
    host_port = 19001,
    buffer_size = 1024,
    connect_timeout_seconds = 2,
    send_interval_seconds = 3,
};

static volatile sig_atomic_t running = 1;

static void stop_running(int signal_number) {
    (void)signal_number;
    running = 0;
}

static void set_timeout(int fd) {
    const struct timeval timeout = {.tv_sec = connect_timeout_seconds, .tv_usec = 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static int make_address(struct sockaddr_in *address, const char *ip, uint16_t port) {
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(port);
    return inet_pton(AF_INET, ip, &address->sin_addr) == 1 ? 0 : -1;
}

static int make_udp_server(void) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in address;
    if (fd < 0 || make_address(&address, "0.0.0.0", guest_port) != 0 ||
        bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("udp server");
        if (fd >= 0) {
            close(fd);
        }
        return -1;
    }
    return fd;
}

static int make_tcp_server(void) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    const int enabled = 1;
    struct sockaddr_in address;
    if (fd < 0) {
        perror("tcp socket");
        return -1;
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    if (make_address(&address, "0.0.0.0", guest_port) != 0 ||
        bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0 || listen(fd, 8) != 0) {
        perror("tcp server");
        close(fd);
        return -1;
    }
    return fd;
}

static void receive_udp(int fd) {
    char buffer[buffer_size];
    struct sockaddr_in peer;
    socklen_t peer_size = sizeof(peer);
    const ssize_t received = recvfrom(fd, buffer, sizeof(buffer) - 1, 0,
                                      (struct sockaddr *)&peer, &peer_size);
    if (received <= 0) {
        return;
    }
    buffer[received] = '\0';
    printf("udp rx: %s\n", buffer);
    fflush(stdout);
    (void)sendto(fd, buffer, (size_t)received, 0, (const struct sockaddr *)&peer, peer_size);
}

static void receive_tcp(int listener) {
    char buffer[buffer_size];
    const int fd = accept(listener, NULL, NULL);
    if (fd < 0) {
        return;
    }
    set_timeout(fd);
    const ssize_t received = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (received > 0) {
        buffer[received] = '\0';
        printf("tcp rx: %s\n", buffer);
        fflush(stdout);
        (void)send(fd, buffer, (size_t)received, 0);
    }
    close(fd);
}

static void send_udp(uint64_t sequence) {
    char message[128];
    char reply[buffer_size];
    struct sockaddr_in host;
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0 || make_address(&host, "10.0.2.2", host_port) != 0) {
        return;
    }
    set_timeout(fd);
    const int length = snprintf(message, sizeof(message), "qemu udp sequence=%llu",
                                (unsigned long long)sequence);
    if (length > 0 && sendto(fd, message, (size_t)length, 0,
                             (const struct sockaddr *)&host, sizeof(host)) >= 0) {
        const ssize_t received = recv(fd, reply, sizeof(reply) - 1, 0);
        if (received > 0) {
            reply[received] = '\0';
            printf("udp echo: %s\n", reply);
            fflush(stdout);
        }
    }
    close(fd);
}

static void send_tcp(uint64_t sequence) {
    char message[128];
    char reply[buffer_size];
    struct sockaddr_in host;
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || make_address(&host, "10.0.2.2", host_port) != 0) {
        return;
    }
    set_timeout(fd);
    const int length = snprintf(message, sizeof(message), "qemu tcp sequence=%llu",
                                (unsigned long long)sequence);
    if (connect(fd, (const struct sockaddr *)&host, sizeof(host)) == 0 && length > 0 &&
        send(fd, message, (size_t)length, 0) >= 0) {
        const ssize_t received = recv(fd, reply, sizeof(reply) - 1, 0);
        if (received > 0) {
            reply[received] = '\0';
            printf("tcp echo: %s\n", reply);
            fflush(stdout);
        }
    }
    close(fd);
}

int main(void) {
    signal(SIGINT, stop_running);
    signal(SIGTERM, stop_running);
    signal(SIGPIPE, SIG_IGN);

    const int udp_fd = make_udp_server();
    const int tcp_fd = make_tcp_server();
    if (udp_fd < 0 || tcp_fd < 0) {
        return EXIT_FAILURE;
    }

    printf("raw TCP/UDP node ready on port %d\n", guest_port);
    fflush(stdout);
    time_t next_send = time(NULL) + 1;
    uint64_t sequence = 1;

    while (running) {
        struct pollfd descriptors[2] = {
            {.fd = udp_fd, .events = POLLIN, .revents = 0},
            {.fd = tcp_fd, .events = POLLIN, .revents = 0},
        };
        const int ready = poll(descriptors, 2, 250);
        if (ready > 0) {
            if ((descriptors[0].revents & POLLIN) != 0) {
                receive_udp(udp_fd);
            }
            if ((descriptors[1].revents & POLLIN) != 0) {
                receive_tcp(tcp_fd);
            }
        } else if (ready < 0 && errno != EINTR) {
            perror("poll");
            break;
        }

        const time_t now = time(NULL);
        if (now >= next_send) {
            send_udp(sequence);
            send_tcp(sequence);
            ++sequence;
            next_send = now + send_interval_seconds;
        }
    }

    close(tcp_fd);
    close(udp_fd);
    return EXIT_SUCCESS;
}
