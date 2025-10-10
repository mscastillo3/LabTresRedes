#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h> // Creacion de sockets nativa de windows
#include <ws2tcpip.h> // Manejo de direcciones IP en windows
#include <time.h>
#pragma comment(lib, "ws2_32.lib")

#define MAX_MSG_LEN 512

int main(int argc, char *argv[]) {

    if (argc != 5) {
        printf("Uso: %s <IP_BROKER> <PUERTO> <TOPIC> <ARCHIVO_MENSAJES>\n", argv[0]);
        return 1;
    }

    char *broker_ip = argv[1];
    int port = atoi(argv[2]);
    char *topic = argv[3];
    char *archivo = argv[4];

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("Error al inicializar Winsock.\n");
        return 1;
    }

    SOCKET sockfd;
    struct sockaddr_in broker_addr;
    char buffer_envio[MAX_MSG_LEN];
    int msg_id = 1;

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

    // Abrir archivo de mensajes
    FILE *file = fopen(archivo, "r");
    if (!file) {
        printf("Error al abrir el archivo %s\n", archivo);
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    printf("[PUBLISHER] Enviando a %s:%d informacion sobre el Partido %s\n", broker_ip, port, topic);

    char mensaje[MAX_MSG_LEN];
    while (fgets(mensaje, sizeof(mensaje), file)) {
        mensaje[strcspn(mensaje, "\n")] = '\0';  // quitar salto de línea

        // Generar timestamp
        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);
        char hora[10]; 
        strftime(hora, sizeof(hora), "%H:%M:%S", tm_info);
        snprintf(buffer_envio, sizeof(buffer_envio), "PUBLISHER|%s|%s|%s", topic, hora, mensaje);
        sendto(sockfd, buffer_envio, strlen(buffer_envio), 0, (struct sockaddr*)&broker_addr, sizeof(broker_addr));
        printf("[PUBLISHER] Mensaje enviado: %s\n", buffer_envio);
        msg_id++;
    }

    printf("[PUBLISHER] Fin del archivo %s.\n", archivo);

    fclose(file);
    closesocket(sockfd);
    WSACleanup();

    return 0;
}
