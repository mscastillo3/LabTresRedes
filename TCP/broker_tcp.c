#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT 9000
#define MAX_CLIENTS 20
#define BUFFER_SIZE 1024
#define TOPIC_LEN 64

typedef struct {
    int socket;
    char topic[TOPIC_LEN];
} Subscriber;

int publisher_sockets[MAX_CLIENTS];
Subscriber subscribers[MAX_CLIENTS];

void iniciar_broker(int *server_fd) {
    struct sockaddr_in address;

    *server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_fd == -1) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

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

    printf("[BROKER] Escuchando en puerto %d...\n", PORT);
}

void registrar_cliente(int new_socket) {
    char buffer[BUFFER_SIZE];
    int bytes = read(new_socket, buffer, BUFFER_SIZE - 1);
    if (bytes <= 0) {
        close(new_socket);
        return;
    }

    buffer[bytes] = '\0';

    if (strncmp(buffer, "PUBLISHER", 9) == 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (publisher_sockets[i] == 0) {
                publisher_sockets[i] = new_socket;
                printf("[BROKER] Publisher conectado: socket %d\n", new_socket);
                return;
            }
        }
    } else if (strncmp(buffer, "SUBSCRIBER|", 11) == 0) {
        char *topic = buffer + 11;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (subscribers[i].socket == 0) {
                subscribers[i].socket = new_socket;
                strncpy(subscribers[i].topic, topic, TOPIC_LEN - 1);
                subscribers[i].topic[TOPIC_LEN - 1] = '\0';
                printf("[BROKER] Subscriber conectado: socket %d, topic '%s'\n", new_socket, subscribers[i].topic);
                return;
            }
        }
    } else {
        printf("[BROKER] Tipo desconocido: %s\n", buffer);
        close(new_socket);
    }
}

void reenviar_a_subscribers(const char *topic, const char *mensaje) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = subscribers[i].socket;
        if (fd > 0 && strstr(subscribers[i].topic, topic) != NULL) {

            if (write(fd, mensaje, strlen(mensaje)) < 0) {
                perror("[BROKER] Error al enviar a subscriber");
            }
        }
    }
}

int main() {
    int server_fd;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    memset(publisher_sockets, 0, sizeof(publisher_sockets));
    memset(subscribers, 0, sizeof(subscribers));

    iniciar_broker(&server_fd);

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (publisher_sockets[i] > 0) {
                FD_SET(publisher_sockets[i], &read_fds);
                if (publisher_sockets[i] > max_fd) max_fd = publisher_sockets[i];
            }
        }

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            continue;
        }

        if (FD_ISSET(server_fd, &read_fds)) {
            int new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
            if (new_socket >= 0) {
                registrar_cliente(new_socket);
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = publisher_sockets[i];
            if (fd > 0 && FD_ISSET(fd, &read_fds)) {
                char buffer[BUFFER_SIZE];
                int bytes = read(fd, buffer, BUFFER_SIZE - 1);
                if (bytes <= 0) {
                    close(fd);
                    publisher_sockets[i] = 0;
                    printf("[BROKER] Publisher desconectado\n");
                } else {
                    buffer[bytes] = '\0';
                    printf("[BROKER] Mensaje recibido: %s\n", buffer);

                    char *tipo = strtok(buffer, "|");
                    char *topic = strtok(NULL, "|");
                    char *hora = strtok(NULL, "|");
                    char *mensaje = strtok(NULL, "");
                    if (tipo && topic && hora && mensaje) {
                        char mensaje_final[BUFFER_SIZE];
                        snprintf(mensaje_final, sizeof(mensaje_final), "[%s] %s: %s\n", hora, topic, mensaje);
                        reenviar_a_subscribers(topic, mensaje_final);
                    }
                }
            }
        }
    }

    return 0;
}