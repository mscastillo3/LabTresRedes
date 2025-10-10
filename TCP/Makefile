
CC = gcc
CFLAGS = -Wall -Wextra -O2
UTILS_SRC = utils_tcp.c
UTILS_HEADER = utils_tcp.h


BROKER = broker
PUBLISHER = publisher
SUBSCRIBER = subscriber


all: $(BROKER) $(PUBLISHER) $(SUBSCRIBER)

$(BROKER): broker_tcp.c $(UTILS_SRC) $(UTILS_HEADER)
	$(CC) $(CFLAGS) broker_tcp.c $(UTILS_SRC) -o $(BROKER)

$(PUBLISHER): publisher_tcp.c $(UTILS_SRC) $(UTILS_HEADER)
	$(CC) $(CFLAGS) publisher_tcp.c $(UTILS_SRC) -o $(PUBLISHER)

$(SUBSCRIBER): subscriber_tcp.c $(UTILS_SRC) $(UTILS_HEADER)
	$(CC) $(CFLAGS) subscriber_tcp.c $(UTILS_SRC) -o $(SUBSCRIBER)


clean:
	rm -f $(BROKER) $(PUBLISHER) $(SUBSCRIBER)


run-broker:
	./$(BROKER)

run-publisher:
	./$(PUBLISHER)

run-subscriber:
	./$(SUBSCRIBER)