#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")  // Enlaza la librería de Winsock

#define BROKER_IP "127.0.0.1"
#define BROKER_PORT 8000
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <topic>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *topic = argv[1];
    SOCKET sock_fd;
    struct sockaddr_in broker_addr;
    char buffer[BUFFER_SIZE];

    // Inicializar Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Error al inicializar Winsock\n");
        return EXIT_FAILURE;
    }

    // Crear socket TCP
    sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd == INVALID_SOCKET) {
        printf("Error al crear socket: %d\n", WSAGetLastError());
        WSACleanup();
        return EXIT_FAILURE;
    }

    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(BROKER_PORT);
    broker_addr.sin_addr.s_addr = inet_addr(BROKER_IP);

    // Conectar al broker
    if (connect(sock_fd, (struct sockaddr *)&broker_addr, sizeof(broker_addr)) == SOCKET_ERROR) {
        printf("Error al conectar con el broker: %d\n", WSAGetLastError());
        closesocket(sock_fd);
        WSACleanup();
        return EXIT_FAILURE;
    }

    printf("[SUBSCRIBER] Conectado al broker en %s:%d\n", BROKER_IP, BROKER_PORT);

    // Enviar identificación
    snprintf(buffer, sizeof(buffer), "SUBSCRIBER|%s\n", topic);
    if (send(sock_fd, buffer, (int)strlen(buffer), 0) == SOCKET_ERROR) {
        printf("Error al enviar identificación: %d\n", WSAGetLastError());
        closesocket(sock_fd);
        WSACleanup();
        return EXIT_FAILURE;
    }

    printf("[SUBSCRIBER] Suscrito al topic '%s'\n", topic);

    // Escuchar mensajes del broker
    while (1) {
        int bytes = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            printf("[SUBSCRIBER] Conexión cerrada por el broker\n");
            break;
        }

        buffer[bytes] = '\0';
        printf("[SUBSCRIBER] Mensaje recibido: %s\n", buffer);
    }

    closesocket(sock_fd);
    WSACleanup();
    return EXIT_SUCCESS;
}
