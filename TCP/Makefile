# Nombre de los ejecutables
BROKER = broker
PUBLISHER = publisher
SUBSCRIBER = subscriber

# Archivos fuente
UTILS_SRC = tcp_utils.c
UTILS_HEADER = tcp_utils.h

# Compilador y flags
CC = gcc
CFLAGS = -Wall -Wextra -O2

# Objetivos por defecto
all: $(BROKER) $(PUBLISHER) $(SUBSCRIBER)

$(BROKER): broker_tcp.c 
	$(CC) $(CFLAGS) broker_tcp.c  -o $(BROKER)

$(PUBLISHER): publisher_tcp.c 
	$(CC) $(CFLAGS) publisher_tcp.c -o $(PUBLISHER)

$(SUBSCRIBER): subscriber_tcp.c  
	$(CC) $(CFLAGS) subscriber_tcp.c  -o $(SUBSCRIBER)

# Limpiar archivos compilados
clean:
	rm -f $(BROKER) $(PUBLISHER) $(SUBSCRIBER)

# Ejecutar cada componente
run-broker:
	./$(BROKER) 

run-publisher:
	./$(PUBLISHER) Partido1.txt 1

run-subscriber:
	./$(SUBSCRIBER) 1

run-publisher2:
	./$(PUBLISHER) Partido2.txt 2

run-subscriber2:
	./$(SUBSCRIBER) 2