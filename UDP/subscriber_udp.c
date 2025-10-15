#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h> // Creacion de sockets nativa de windows
#include <ws2tcpip.h> // Manejo de direcciones IP en windows
#pragma comment(lib, "ws2_32.lib")

#define MAX_MSG_LEN 512

int main(int argc, char *argv[]) {

    if (argc != 4) {
        printf("Uso: %s <IP_BROKER> <PUERTO> <TOPIC>\n", argv[0]);
        return 1;
    }

    char *broker_ip = argv[1];
    int port = atoi(argv[2]);
    char *topic = argv[3];

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("Error al inicializar Winsock.\n");
        return 1;
    }

    SOCKET sockfd;
    struct sockaddr_in broker_addr;
    char buffer[MAX_MSG_LEN];
    char msg[MAX_MSG_LEN];

    // Crear socket 
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET) {
        printf("Error al crear socket.\n");
        WSACleanup();
        return 1;
    }

    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(port);
    broker_addr.sin_addr.s_addr = inet_addr(broker_ip);

    // Enviar mensaje de suscripcion
    snprintf(msg, sizeof(msg), "SUBSCRIBER|%s", topic);
    sendto(sockfd, msg, strlen(msg), 0,
           (struct sockaddr*)&broker_addr, sizeof(broker_addr));

    printf("[SUBSCRIBER] Suscrito al partido %s\n", topic);

    // Recibir mensajes del broker
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = recvfrom(sockfd, buffer, MAX_MSG_LEN, 0, NULL, NULL);
        if (n > 0) {
            buffer[n] = '\0';
            printf("[SUBSCRIBER] Mensaje recibido: %s\n", buffer);
        }
    }

    closesocket(sockfd);
    WSACleanup();
    return 0;
}
