#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h> // Creacion de sockets nativa de windows
#include <ws2tcpip.h> // Manejo de direcciones IP en windows
#pragma comment(lib, "ws2_32.lib")  

#define MAX_MSG_LEN 512
#define MAX_SUBS 100

// Estructura que guarda un subscriptor (topic + dirección)
typedef struct {
    char topic[50];
    struct sockaddr_in addr;
} Subscriber;

Subscriber subscribers[MAX_SUBS];
int subscriber_count = 0;

int main(int argc, char *argv[]) {

    // Verificación de argumentos
    if (argc != 2) {
        printf("Uso: %s <PUERTO>\n", argv[0]);
        return 1;
    }

    // Inicialización de Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("Error al inicializar Winsock.\n");
        return 1;
    }

    int port = atoi(argv[1]);
    SOCKET sockfd;
    struct sockaddr_in broker_addr, client_addr;
    char buffer[MAX_MSG_LEN];
    int addr_len = sizeof(client_addr);

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
    broker_addr.sin_addr.s_addr = INADDR_ANY;
    broker_addr.sin_port = htons(port);

    // Enlazar socket al puerto
    if (bind(sockfd, (struct sockaddr*)&broker_addr, sizeof(broker_addr)) == SOCKET_ERROR) {
        printf("Error al hacer bind.\n");
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    printf("[BROKER UDP] Escuchando en puerto %d...\n", port);

    // Bucle principal de recepción
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = recvfrom(sockfd, buffer, MAX_MSG_LEN, 0,(struct sockaddr*)&client_addr, &addr_len);
        if (n == SOCKET_ERROR) {
            printf("Error al recibir mensaje.\n");
            continue;
        }

        buffer[n] = '\0'; // Añadir terminador de cadena

        // Si el mensaje comienza con "SUB|"
        if (strncmp(buffer, "SUBSCRIBER|", 11) == 0) {
            char *topic = buffer + 11;

            if (subscriber_count < MAX_SUBS) {
                strcpy(subscribers[subscriber_count].topic, topic);
                subscribers[subscriber_count].addr = client_addr;
                subscriber_count++;
                printf("[BROKER] Nuevo subscriptor a 'Partido %s'\n", topic);
            }

        }
        // 🔹 Procesar publicación
        else if (strncmp(buffer, "PUBLISHER|", 10) == 0) {

            char *topic = strtok(buffer + 10, "|");
            char *hora = strtok(NULL, "|");
            char *mensaje = strtok(NULL, "");

            if (topic && hora && mensaje) {
                printf("[BROKER] Publicacion recibida del partido '%s': %s|%s\n", topic, hora, mensaje);

                // Reenviar a los subscriptores interesados
                for (int i = 0; i < subscriber_count; i++) {
                    if (strcmp(subscribers[i].topic, topic) == 0) {
                        sendto(sockfd, mensaje, strlen(mensaje), 0, (struct sockaddr*)&subscribers[i].addr, sizeof(subscribers[i].addr));
                    }
                }
            }
        }
    }

    closesocket(sockfd);
    WSACleanup();
    return 0;
}
