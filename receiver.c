#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>

#define RELAY_IN_PORT 47002
#define HARNESS_OUT_PORT 47020

int main() {
    int in_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(RELAY_IN_PORT);
    in_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(in_sock, (struct sockaddr *)&in_addr, sizeof(in_addr));

    int out_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in out_addr = {0};
    out_addr.sin_family = AF_INET;
    out_addr.sin_port = htons(HARNESS_OUT_PORT);
    out_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    struct pollfd pfd = { .fd = in_sock, .events = POLLIN };
    
    char buf[2000];

    while (1) {
        int n = poll(&pfd, 1, 40); // 40ms timeout
        if (n > 0 && (pfd.revents & POLLIN)) {
            int len = recv(in_sock, buf, sizeof(buf), 0);
            if (len > 0) {
                // blindly forward to playout
                sendto(out_sock, buf, len, 0, (struct sockaddr *)&out_addr, sizeof(out_addr));
            }
        }
    }

    return 0;
}
