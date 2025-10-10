#include "utils_tcp.h"
#include "stdio.h"

#define PORT 9000
#define MAX_CLIENTS 20
#define BUFFER_SIZE 1024

int publisher_sockets[MAX_CLIENTS];
int subscriber_sockets[MAX_CLIENTS];

// syscall numbers for x86_64 Linux
#define SYS_read 0
#define SYS_write 1

void iniciar_broker(int *server_fd) {
    *server_fd = preparar_servidor_tcp(PORT);
}

void registrar_cliente(int new_socket) {
    char tipo[BUFFER_SIZE];
    long bytes = syscall(SYS_read, new_socket, tipo, BUFFER_SIZE);

    if (bytes <= 0) {
        cerrar_socket(new_socket);
        return;
    }

    tipo[bytes] = '\0';

    if (tipo[0] == 'P') { // "PUBLISHER"
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (publisher_sockets[i] == 0) {
                publisher_sockets[i] = new_socket;
                break;
            }
        }
    } else if (tipo[0] == 'S') { // "SUBSCRIBER"
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (subscriber_sockets[i] == 0) {
                subscriber_sockets[i] = new_socket;
                break;
            }
        }
    } else {
        cerrar_socket(new_socket);
    }
}

void reenviar_a_subscribers(char *msg, long len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = subscriber_sockets[i];
        if (fd > 0) {
            syscall(SYS_write, fd, msg, len);
        }
    }
}

void manejar_mensajes() {
    char buffer[BUFFER_SIZE];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = publisher_sockets[i];
        if (fd > 0) {
            long bytes = syscall(SYS_read, fd, buffer, BUFFER_SIZE);
            if (bytes <= 0) {
                cerrar_socket(fd);
                publisher_sockets[i] = 0;
            } else {
                reenviar_a_subscribers(buffer, bytes);
            }
        }
    }
}

int main() {
    int server_fd;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        publisher_sockets[i] = 0;
        subscriber_sockets[i] = 0;
    }

    iniciar_broker(&server_fd);

    while (1) {
        int new_socket = aceptar_conexion(server_fd);
        if (new_socket >= 0) {
            registrar_cliente(new_socket);
        }

        manejar_mensajes();
    }

    cerrar_socket(server_fd);
    return 0;
}