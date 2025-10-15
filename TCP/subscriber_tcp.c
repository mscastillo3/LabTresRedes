#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BROKER_IP "127.0.0.1"
#define BROKER_PORT 9000
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <topic>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *topic = argv[1];
    int sock_fd;
    struct sockaddr_in broker_addr;
    char buffer[BUFFER_SIZE];

    // Crear socket TCP
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Error al crear socket");
        return EXIT_FAILURE;
    }

    // Configurar dirección del broker
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(BROKER_PORT);
    broker_addr.sin_addr.s_addr = inet_addr(BROKER_IP);

    // Conectar al broker
    if (connect(sock_fd, (struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0) {
        perror("Error al conectar con el broker");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("[SUBSCRIBER] Conectado al broker en %s:%d\n", BROKER_IP, BROKER_PORT);

    // Identificarse como SUBSCRIBER y enviar topic
    snprintf(buffer, sizeof(buffer), "SUBSCRIBER|%s\n", topic);
    if (write(sock_fd, buffer, strlen(buffer)) < 0) {
        perror("Error al enviar identificación");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("[SUBSCRIBER] Suscrito al topic '%s'\n", topic);

    // Escuchar mensajes del broker
    while (1) {
        int bytes = read(sock_fd, buffer, BUFFER_SIZE - 1);
        if (bytes <= 0) {
            printf("Conexión cerrada por el broker\n");
            break;
        }

        buffer[bytes] = '\0';
        printf("[SUBSCRIBER] Mensaje recibido: %s\n", buffer);
    }

    close(sock_fd);
    return EXIT_SUCCESS;
}