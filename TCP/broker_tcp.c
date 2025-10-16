#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8000
#define MAX_CLIENTS 20
#define BUFFER_SIZE 1024
#define TOPIC_LEN 64

typedef struct {
    SOCKET socket;
    char topic[TOPIC_LEN];
} Subscriber;

SOCKET publisher_sockets[MAX_CLIENTS];
Subscriber subscribers[MAX_CLIENTS];

void iniciar_broker(SOCKET *server_fd) {
    struct sockaddr_in address;

    *server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*server_fd == INVALID_SOCKET) {
        perror("Error al crear socket");
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(*server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
        perror("Error en bind");
        closesocket(*server_fd);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    if (listen(*server_fd, MAX_CLIENTS) == SOCKET_ERROR) {
        perror("Error en listen");
        closesocket(*server_fd);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    printf("[BROKER] Escuchando en puerto %d...\n", PORT);
}

void registrar_cliente(SOCKET new_socket) {
    char buffer[BUFFER_SIZE];
    int bytes = recv(new_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        closesocket(new_socket);
        return;
    }

    buffer[bytes] = '\0';

    if (strncmp(buffer, "PUBLISHER", 9) == 0) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (publisher_sockets[i] == 0) {
                publisher_sockets[i] = new_socket;
                printf("[BROKER] Publisher conectado: socket %d\n", (int)new_socket);
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
                printf("[BROKER] Subscriber conectado: socket %d, topic '%s'\n", (int)new_socket, subscribers[i].topic);
                return;
            }
        }
    } else {
        printf("[BROKER] Tipo desconocido: %s\n", buffer);
        closesocket(new_socket);
    }
}

void reenviar_a_subscribers(const char *topic, const char *mensaje) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        SOCKET fd = subscribers[i].socket;
        if (fd > 0 && strstr(subscribers[i].topic, topic) != NULL) {
            if (send(fd, mensaje, (int)strlen(mensaje), 0) == SOCKET_ERROR) {
                perror("[BROKER] Error al enviar a subscriber");
            }
        }
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Error al iniciar Winsock.\n");
        return EXIT_FAILURE;
    }

    SOCKET server_fd;
    struct sockaddr_in client_addr;
    int addrlen = sizeof(client_addr);

    memset(publisher_sockets, 0, sizeof(publisher_sockets));
    memset(subscribers, 0, sizeof(subscribers));

    iniciar_broker(&server_fd);

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        SOCKET max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (publisher_sockets[i] > 0) {
                FD_SET(publisher_sockets[i], &read_fds);
                if (publisher_sockets[i] > max_fd) max_fd = publisher_sockets[i];
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (subscribers[i].socket > 0) {
                FD_SET(subscribers[i].socket, &read_fds);
                if (subscribers[i].socket > max_fd) max_fd = subscribers[i].socket;
            }
        }

        int activity = select(0, &read_fds, NULL, NULL, NULL);
        if (activity == SOCKET_ERROR) {
            perror("select");
            continue;
        }

        if (FD_ISSET(server_fd, &read_fds)) {
            SOCKET new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
            if (new_socket != INVALID_SOCKET) {
                registrar_cliente(new_socket);
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            SOCKET fd = publisher_sockets[i];
            if (fd > 0 && FD_ISSET(fd, &read_fds)) {
                char buffer[BUFFER_SIZE];
                int bytes = recv(fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytes <= 0) {
                    closesocket(fd);
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

    closesocket(server_fd);
    WSACleanup();
    return 0;
}
