#include <arpa/inet.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HARNESS_IN_PORT 47010
#define RELAY_OUT_PORT 47001
#define RELAY_ARQ_IN_PORT 47004

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

  int nack_sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in nack_addr = {0};
  nack_addr.sin_family = AF_INET;
  nack_addr.sin_port = htons(RELAY_ARQ_IN_PORT);
  nack_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(nack_sock, (struct sockaddr *)&nack_addr, sizeof(nack_addr));

  struct pollfd pfds[2];
  pfds[0].fd = in_sock; pfds[0].events = POLLIN;
  pfds[1].fd = nack_sock; pfds[1].events = POLLIN;

  char *duration_str = getenv("DURATION_S");
  double duration = duration_str ? atof(duration_str) : 30.0;
  size_t BUF_SIZE = (size_t)(duration * 50 + 100);
  if (BUF_SIZE < 1024) BUF_SIZE = 1024;
  
  char *frame_buf = calloc(BUF_SIZE, 160);
  bool *nack_pending = calloc(BUF_SIZE, sizeof(bool));
  uint8_t *resend_count = calloc(BUF_SIZE, sizeof(uint8_t));
  uint32_t *nack_queue = calloc(BUF_SIZE, sizeof(uint32_t));
  size_t nack_head = 0, nack_tail = 0;

  char buf[2000];
  long budget = 0;

  while (1) {
    int n = poll(pfds, 2, -1);
    
    // Drain incoming NACKs
    if (n > 0 && (pfds[1].revents & POLLIN)) {
      char nack_buf[64];
      while (1) {
        int len = recv(nack_sock, nack_buf, sizeof(nack_buf), MSG_DONTWAIT);
        if (len < 0) break;
        if (len == 2) {
          budget -= 2; // NACKs cost 2 bytes of budget
          
          uint16_t missing_seq_net;
          memcpy(&missing_seq_net, nack_buf, 2);
          uint32_t missing_seq = ntohs(missing_seq_net);
          
          size_t idx = missing_seq % BUF_SIZE;
          if (!nack_pending[idx] && resend_count[idx] < 2) {
            nack_pending[idx] = true;
            nack_queue[nack_tail % BUF_SIZE] = missing_seq;
            nack_tail++;
          }
        }
      }
    }

    if (n > 0 && (pfds[0].revents & POLLIN)) {
      int len = recv(in_sock, buf, sizeof(buf), 0);
      if (len == 164) {
        budget += 310;
        if (budget > 1000) {
          budget = 1000;
        }
        
        uint32_t harness_seq;
        memcpy(&harness_seq, buf, 4);
        harness_seq = ntohl(harness_seq);

        // Store payload in circular buffer
        memcpy(frame_buf + (harness_seq % BUF_SIZE) * 160, buf + 4, 160);
        
        bool sent = false;

        // Try to service one NACK if queue isn't empty
        if (budget >= 323 && nack_head < nack_tail) {
            uint32_t missing_seq = nack_queue[nack_head % BUF_SIZE];
            nack_head++;
            size_t idx = missing_seq % BUF_SIZE;
            nack_pending[idx] = false;
            
            if (resend_count[idx] < 2 && harness_seq >= missing_seq && (harness_seq - missing_seq) <= 255) {
                resend_count[idx]++;
                
                char out_buf[323];
                uint16_t internal_seq = htons((uint16_t)harness_seq);
                memcpy(out_buf, &internal_seq, 2);
                memcpy(out_buf + 2, buf + 4, 160);
                
                out_buf[162] = (uint8_t)(harness_seq - missing_seq);
                memcpy(out_buf + 163, frame_buf + idx * 160, 160);
                
                budget -= 323;
                sendto(out_sock, out_buf, 323, 0, (struct sockaddr *)&out_addr, sizeof(out_addr));
                sent = true;
            }
        }

        if (!sent) {
            if (budget >= 323 && harness_seq >= 1) {
                // Construct full packet: 2B seq + 160B payload + 1B offset + 160B fec_payload
                char out_buf[323];
                uint16_t internal_seq = htons((uint16_t)harness_seq);
                memcpy(out_buf, &internal_seq, 2);
                memcpy(out_buf + 2, buf + 4, 160);
                
                out_buf[162] = 1; // fec_offset
                memcpy(out_buf + 163, frame_buf + ((harness_seq - 1) % BUF_SIZE) * 160, 160);
                
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
  }

  return 0;
}
