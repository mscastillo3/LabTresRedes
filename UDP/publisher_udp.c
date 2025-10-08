#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>

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

    // Inicializar Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("Error al inicializar Winsock.\n");
        return 1;
    }

    SOCKET sockfd;
    struct sockaddr_in broker_addr;
    char msg[MAX_MSG_LEN];

    // Crear socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET) {
        printf("Error al crear socket.\n");
        WSACleanup();
        return 1;
    }

    // Configurar dirección del broker
    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(port);
    inet_pton(AF_INET, broker_ip, &broker_addr.sin_addr);

    printf("[PUBLISHER UDP] Enviando a %s:%d topic %s\n", broker_ip, port, topic);

    // Enviar 10 mensajes de ejemplo
    for (int i = 1; i <= 10; i++) {
        char timestamp[30];
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", t);

        snprintf(msg, sizeof(msg), "PUB|%s|%d|%s|Evento %d en %s",
                 topic, i, timestamp, i, topic);

        sendto(sockfd, msg, strlen(msg), 0,
               (struct sockaddr*)&broker_addr, sizeof(broker_addr));

        printf("[PUBLISHER] Mensaje enviado: %s\n", msg);
        Sleep(1000); // Esperar 1 segundo
    }

    closesocket(sockfd);
    WSACleanup();
    return 0;
}
