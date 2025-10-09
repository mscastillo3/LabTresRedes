#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9000
#define MAX_CLIENTS 20
#define BUFFER_SIZE 1024

int publisher_sockets[MAX_CLIENTS];
int subscriber_sockets[MAX_CLIENTS];

void iniciar_broker(int *server_fd) {
    struct sockaddr_in address;

    *server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_fd == -1) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(*server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Error en bind");
        exit(EXIT_FAILURE);
    }

    if (listen(*server_fd, MAX_CLIENTS) < 0) {
        perror("Error en listen");
        exit(EXIT_FAILURE);
    }

    printf("Broker TCP escuchando en puerto %d...\n", PORT);
}

void registrar_cliente(int new_socket) {
    char tipo[BUFFER_SIZE];
    int bytes = read(new_socket, tipo, BUFFER_SIZE);
    tipo[bytes] = '\0';

    if (strncmp(tipo, "PUBLISHER", 9) == 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (publisher_sockets[i] == 0) {
                publisher_sockets[i] = new_socket;
                printf("Publisher conectado: socket %d\n", new_socket);
                break;
            }
        }
    } else if (strncmp(tipo, "SUBSCRIBER", 10) == 0) {M
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (subscriber_sockets[i] == 0) {
                subscriber_sockets[i] = new_socket;
                printf("Subscriber conectado: socket %d\n", new_socket);
                break;
            }
        }
    } else {
        printf("Tipo desconocido: %s\n", tipo);
        close(new_socket);
    }
}

void reenviar_a_subscribers(char *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = subscriber_sockets[i];
        if (fd > 0) {
            if (write(fd, msg, strlen(msg)) < 0) {
                perror("Error al enviar tipo");
            }

        }
    }
}

void manejar_mensajes() {
    char buffer[BUFFER_SIZE];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = publisher_sockets[i];
        if (fd > 0) {
            int bytes = read(fd, buffer, BUFFER_SIZE);
            if (bytes <= 0) {
                close(fd);
                publisher_sockets[i] = 0;
                printf("Publisher desconectado\n");
            } else {
                buffer[bytes] = '\0';
                printf("Mensaje recibido de publisher: %s\n", buffer);
                reenviar_a_subscribers(buffer);
            }
        }
    }
}

int main() {
    int server_fd;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        publisher_sockets[i] = 0;
        subscriber_sockets[i] = 0;
    }

    iniciar_broker(&server_fd);

    while (1) {
        int new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (new_socket >= 0) {
            registrar_cliente(new_socket);
        }

        manejar_mensajes();
    }

    return 0;
}