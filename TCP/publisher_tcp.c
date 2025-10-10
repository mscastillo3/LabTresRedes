#include "utils_tcp.h"

#define BROKER_IP 0x0100007F  // 127.0.0.1 en hexadecimal (little endian)
#define BROKER_PORT 9000
#define BUFFER_SIZE 1024

// syscall numbers para x86_64 Linux
#define SYS_write 1

int main() {
    int sock_fd = conectar_a_servidor(BROKER_IP, BROKER_PORT);

    // Identificarse como PUBLISHER
    syscall(SYS_write, sock_fd, "PUBLISHER", 9);

    // Enviar mensajes manualmente
    char mensaje[] = "mensaje";
    while (1) {
        syscall(SYS_write, sock_fd, mensaje, 7);
    }

    cerrar_socket(sock_fd);
    return 0;
}