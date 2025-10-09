#include "tcp_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int crear_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }
    return fd;
}

int conectar_a(const char *ip, int puerto) {
    int fd = crear_socket();
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(puerto);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Error al conectar");
        close(fd);
        return -1;
    }

    return fd;
}

int enviar(int fd, const char *msg) {
    int len = strlen(msg);
    int sent = write(fd, msg, len);
    if (sent < 0) {
        perror("Error al enviar");
        return -1;
    }
    return sent;
}

int recibir(int fd, char *buffer, int size) {
    int bytes = read(fd, buffer, size);
    if (bytes < 0) {
        perror("Error al recibir");
        return -1;
    }
    buffer[bytes] = '\0';
    return bytes;
}
