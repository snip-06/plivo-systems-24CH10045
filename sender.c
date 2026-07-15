#include <arpa/inet.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HARNESS_IN_PORT 47010
#define RELAY_OUT_PORT 47001

int main() {
  char *t0_str = getenv("T0");
  if (!t0_str) {
    fprintf(stderr, "Missing T0 env\n");
    return 1;
  }
  double t0_env = atof(t0_str);

  struct timespec t0;
  t0.tv_sec = (time_t)t0_env;
  t0.tv_nsec = (long)((t0_env - t0.tv_sec) * 1e9);

  int in_sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in in_addr = {0};
  in_addr.sin_family = AF_INET;
  in_addr.sin_port = htons(HARNESS_IN_PORT);
  in_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(in_sock, (struct sockaddr *)&in_addr, sizeof(in_addr));

  int out_sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in out_addr = {0};
  out_addr.sin_family = AF_INET;
  out_addr.sin_port = htons(RELAY_OUT_PORT);
  out_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  struct pollfd pfd = {.fd = in_sock, .events = POLLIN};

  char *duration_str = getenv("DURATION_S");
  double duration = duration_str ? atof(duration_str) : 30.0;
  size_t BUF_SIZE = (size_t)(duration * 50 + 100);
  if (BUF_SIZE < 1024) BUF_SIZE = 1024;
  
  char *frame_buf = calloc(BUF_SIZE, 160);

  char buf[2000];
  long budget = 0;

  while (1) {
    int n = poll(&pfd, 1, -1); // Block until a packet arrives
    if (n > 0 && (pfd.revents & POLLIN)) {
      int len = recv(in_sock, buf, sizeof(buf), 0);
      if (len == 164) {
        budget += 310;
        if (budget > 1000) {
          budget = 1000;
        }
        // Harness provides 4-byte uint32_t seq (network order) + 160-byte
        // payload
        uint32_t harness_seq;
        memcpy(&harness_seq, buf, 4);
        harness_seq = ntohl(harness_seq);

        // Store payload in circular buffer
        memcpy(frame_buf + (harness_seq % BUF_SIZE) * 160, buf + 4, 160);

        if (budget >= 323 && harness_seq >= 2) {
            // Construct full packet: 2B seq + 160B payload + 1B offset + 160B fec_payload
            char out_buf[323];
            uint16_t internal_seq = htons((uint16_t)harness_seq);
            memcpy(out_buf, &internal_seq, 2);
            memcpy(out_buf + 2, buf + 4, 160);
            
            out_buf[162] = 2; // fec_offset
            memcpy(out_buf + 163, frame_buf + ((harness_seq - 2) % BUF_SIZE) * 160, 160);
            
            budget -= 323;
            sendto(out_sock, out_buf, 323, 0, (struct sockaddr *)&out_addr, sizeof(out_addr));
        } else if (budget >= 162) {
            // Construct small packet
            char out_buf[162];
            uint16_t internal_seq = htons((uint16_t)harness_seq);
            memcpy(out_buf, &internal_seq, 2);
            memcpy(out_buf + 2, buf + 4, 160);
            
            budget -= 162;
            sendto(out_sock, out_buf, 162, 0, (struct sockaddr *)&out_addr, sizeof(out_addr));
        }
      }
    }
  }

  return 0;
}
