CFLAGS = -Wall -Wextra -O2 -g

all: sender receiver

sender: sender.c
	$(CC) $(CFLAGS) -o $@ $^

receiver: receiver.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f sender receiver
