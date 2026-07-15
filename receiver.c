#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <stdint.h>
#include <stdbool.h>

#define RELAY_IN_PORT 47002
#define HARNESS_OUT_PORT 47020

int main() {
    char *duration_str = getenv("DURATION_S");
    double duration = duration_str ? atof(duration_str) : 30.0;
    
    // Calculate dynamic BUF_SIZE for deduplication
    size_t BUF_SIZE = (size_t)(duration * 50 + 100);
    if (BUF_SIZE < 1024) BUF_SIZE = 1024;
    
    bool *played = calloc(BUF_SIZE, sizeof(bool));

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
    uint32_t highest_seq = 0;
    bool has_received = false;

    while (1) {
        int n = poll(&pfd, 1, 40); 
        if (n > 0 && (pfd.revents & POLLIN)) {
            int len = recv(in_sock, buf, sizeof(buf), 0);
            if (len >= 162) {
                // Read 2-byte internal sequence number
                uint16_t internal_seq_net;
                memcpy(&internal_seq_net, buf, 2);
                uint16_t internal_seq = ntohs(internal_seq_net);
                
                // Widen back to 32-bit sequence number (handling epoch wraps)
                uint32_t epoch = highest_seq & 0xFFFF0000;
                uint32_t seq32 = epoch | internal_seq;
                
                if (has_received) {
                    if (seq32 < highest_seq && (highest_seq - seq32) > 32768) {
                        seq32 += 0x10000; // Wrapped around forward
                    } else if (seq32 > highest_seq && (seq32 - highest_seq) > 32768) {
                        seq32 -= 0x10000; // Old packet from previous epoch
                    }
                }
                
                if (!has_received || seq32 > highest_seq) {
                    highest_seq = seq32;
                    has_received = true;
                }

                // Check played bitmap to drop duplicates
                if (played[seq32 % BUF_SIZE]) {
                    continue; // Silently drop duplicate packet
                }
                played[seq32 % BUF_SIZE] = true;

                // Reconstruct harness packet: 4-byte seq (network order) + 160-byte payload
                char out_buf[164];
                uint32_t harness_seq_net = htonl(seq32);
                memcpy(out_buf, &harness_seq_net, 4);
                memcpy(out_buf + 4, buf + 2, 160);
                
                sendto(out_sock, out_buf, 164, 0, (struct sockaddr *)&out_addr, sizeof(out_addr));
            }
        }
    }

    free(played);
    return 0;
}
