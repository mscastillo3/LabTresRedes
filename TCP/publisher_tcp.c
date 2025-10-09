#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>             // read(), write(), close()
#include <sys/socket.h>         // socket(), connect()
#include <netinet/in.h>         // sockaddr_in
#include <arpa/inet.h>          // inet_addr()

#define BROKER_IP "127.0.0.1"
#define BROKER_PORT 9000
#define BUFFER_SIZE 1024

int main() {
    int sock_fd;
    struct sockaddr_in broker_addr;
    char buffer[BUFFER_SIZE];

    // Crear socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    // Configurar dirección del broker
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(BROKER_PORT);
    broker_addr.sin_addr.s_addr = inet_addr(BROKER_IP);

    // Conectar al broker
    if (connect(sock_fd, (struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0) {
        perror("Error al conectar con el broker");
        exit(EXIT_FAILURE);
    }

    printf("Publisher conectado al broker en %s:%d\n", BROKER_IP, BROKER_PORT);

    // Identificarse como PUBLISHER
   if (write(sock_fd, "PUBLISHER", strlen("PUBLISHER")) < 0) {
        perror("Error al enviar tipo");
    }

    // Enviar mensajes manualmente
    while (1) {
        printf("Escribe un mensaje para publicar: ");
        if (!fgets(buffer, BUFFER_SIZE, stdin)) {
            fprintf(stderr, "Error al leer entrada\n");
            continue;
        }

        // Eliminar salto de línea
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strlen(buffer) == 0) continue;
        if (strcmp(buffer, "exit") == 0) break;

       if (write( sock_fd, buffer, strlen(buffer)) < 0) {
            perror("Error al enviar tipo");
        }

    }

    close(sock_fd);
    return 0;
}