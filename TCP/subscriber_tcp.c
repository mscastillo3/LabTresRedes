#include "utils_tcp.h"
#include "stdio.h"

#define BROKER_IP 0x0100007F  // 127.0.0.1 en hexadecimal (little endian)
#define BROKER_PORT 9000
#define BUFFER_SIZE 1024

// syscall numbers para x86_64 Linux
#define SYS_read 0
#define SYS_write 1

int main() {
    int sock_fd = conectar_a_servidor(BROKER_IP, BROKER_PORT);

    // Identificarse como SUBSCRIBER
    syscall(SYS_write, sock_fd, "SUBSCRIBER", 10);

    char buffer[BUFFER_SIZE];

    // Escuchar mensajes del broker
    while (1) {
        long bytes = syscall(SYS_read, sock_fd, buffer, BUFFER_SIZE);
        if (bytes <= 0) {
            break;  // Conexión cerrada
        }

        buffer[bytes] = '\0';
        printf("Mensaje recibido: %s\n", buffer);
    }

    cerrar_socket(sock_fd);
    return 0;
}