#include <arpa/inet.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

uint64_t current_time_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

#define RELAY_IN_PORT 47002
#define HARNESS_OUT_PORT 47020
#define RELAY_ARQ_OUT_PORT 47003

int main() {
  char *duration_str = getenv("DURATION_S");
  double duration = duration_str ? atof(duration_str) : 30.0;

  // Calculate dynamic BUF_SIZE for deduplication
  size_t BUF_SIZE = (size_t)(duration * 50 + 100);
  if (BUF_SIZE < 1024)
    BUF_SIZE = 1024;

  bool *played = calloc(BUF_SIZE, sizeof(bool));
  uint8_t *nack_count = calloc(BUF_SIZE, sizeof(uint8_t));
  uint64_t *last_nack_time = calloc(BUF_SIZE, sizeof(uint64_t));
  uint64_t *first_miss_time = calloc(BUF_SIZE, sizeof(uint64_t));

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

  int arq_out_sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in arq_out_addr = {0};
  arq_out_addr.sin_family = AF_INET;
  arq_out_addr.sin_port = htons(RELAY_ARQ_OUT_PORT);
  arq_out_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  struct pollfd pfd = {.fd = in_sock, .events = POLLIN};

  char buf[2000];
  uint32_t highest_seq = 0;
  bool has_received = false;

  while (1) {
    int n = poll(&pfd, 1, 40);
    if (n > 0 && (pfd.revents & POLLIN)) {
      int len = recv(in_sock, buf, sizeof(buf), 0);
      if (len == 162 || len == 323) {
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

        // Process the primary payload
        if (!played[seq32 % BUF_SIZE]) {
          played[seq32 % BUF_SIZE] = true;
          
          char out_buf[164];
          uint32_t harness_seq_net = htonl(seq32);
          memcpy(out_buf, &harness_seq_net, 4);
          memcpy(out_buf + 4, buf + 2, 160);
          
          sendto(out_sock, out_buf, 164, 0, (struct sockaddr *)&out_addr, sizeof(out_addr));
        }

        // If FEC is attached, try to recover it
        if (len == 323) {
          uint8_t fec_offset = buf[162];
          if (seq32 >= fec_offset) {
            uint32_t fec_seq = seq32 - fec_offset;
            
            if (!played[fec_seq % BUF_SIZE]) {
              played[fec_seq % BUF_SIZE] = true;
              
              char out_buf[164];
              uint32_t fec_harness_seq_net = htonl(fec_seq);
              memcpy(out_buf, &fec_harness_seq_net, 4);
              memcpy(out_buf + 4, buf + 163, 160);
              
              sendto(out_sock, out_buf, 164, 0, (struct sockaddr *)&out_addr, sizeof(out_addr));
            }
          }
        }
      }
    }

    // Gap detection & NACK sending
    uint64_t now = current_time_ms();
    uint32_t start_seq = (highest_seq > 50) ? highest_seq - 50 : 0;
    for (uint32_t s = start_seq; s < highest_seq; s++) {
        size_t idx = s % BUF_SIZE;
        if (!played[idx]) {
            if (first_miss_time[idx] == 0) first_miss_time[idx] = now;
            
            bool should_nack = false;
            if (nack_count[idx] == 0) {
                // Send NACK if there's a big gap or it's been missing for >40ms
                if (highest_seq > s + 2 || (now - first_miss_time[idx] >= 40)) {
                    should_nack = true;
                }
            } else if (nack_count[idx] < 2) {
                // Send retry NACK if 85ms timeout expired
                if (now - last_nack_time[idx] >= 85) {
                    should_nack = true;
                }
            }
            
            if (should_nack) {
                nack_count[idx]++;
                last_nack_time[idx] = now;
                
                uint16_t missing_seq_net = htons((uint16_t)s);
                sendto(arq_out_sock, &missing_seq_net, 2, 0, (struct sockaddr *)&arq_out_addr, sizeof(arq_out_addr));
            }
        }
    }
  }

  free(played);
  free(nack_count);
  free(last_nack_time);
  free(first_miss_time);
  return 0;
}
