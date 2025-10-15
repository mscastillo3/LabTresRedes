#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>             
#include <sys/socket.h>         
#include <netinet/in.h>         
#include <arpa/inet.h>          
#include <time.h>               

#define BROKER_IP "127.0.0.1"
#define BROKER_PORT 9000
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo_mensajes> <partido>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *archivo = argv[1];
    const char *partido = argv[2];

    int sock_fd;
    struct sockaddr_in broker_addr;
    char buffer_envio[BUFFER_SIZE];

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

    printf("[PUBLISHER] Conectado al broker en %s:%d\n", BROKER_IP, BROKER_PORT);

    // Identificarse como PUBLISHER con partido
    snprintf(buffer_envio, sizeof(buffer_envio), "PUBLISHER|%s\n", partido);
    if (write(sock_fd, buffer_envio, strlen(buffer_envio)) < 0) {
        perror("Error al enviar identificación");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    // Abrir archivo de mensajes
    FILE *file = fopen(archivo, "r");
    if (!file) {
        perror("Error al abrir el archivo");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("[PUBLISHER] Enviando mensajes del archivo '%s' para el partido '%s'\n", archivo, partido);

    char mensaje[BUFFER_SIZE];
    while (fgets(mensaje, sizeof(mensaje), file)) {
        mensaje[strcspn(mensaje, "\n")] = '\0';  // quitar salto de línea

        // Generar timestamp
        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);
        char hora[9];  // HH:MM:SS
        strftime(hora, sizeof(hora), "%H:%M:%S", tm_info);

        // Formatear mensaje
        char mensaje_limpio[900];
        strncpy(mensaje_limpio, mensaje, sizeof(mensaje_limpio) - 1);
        mensaje_limpio[sizeof(mensaje_limpio) - 1] = '\0';


        snprintf(buffer_envio, sizeof(buffer_envio), "PUBLISHER|%s|%s|%s\n", partido, hora, mensaje_limpio);

        // Enviar mensaje
        if (write(sock_fd, buffer_envio, strlen(buffer_envio)) < 0) {
            perror("Error al enviar mensaje");
            break;
        }

        printf("[PUBLISHER] Mensaje enviado: %s\n", buffer_envio);
        sleep(5);
    }

    printf("[PUBLISHER] Fin del archivo '%s'. Cerrando conexión.\n", archivo);

    fclose(file);
    close(sock_fd);
    return EXIT_SUCCESS;
}