#include "utils_tcp.h"

#define SYS_socket     41
#define SYS_bind       49
#define SYS_listen     50
#define SYS_accept     43
#define SYS_connect    42
#define SYS_close      3

#define AF_INET        2
#define SOCK_STREAM    1
#define INADDR_ANY     0

typedef unsigned long size_t;
typedef unsigned short sa_family_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

struct sockaddr_in {
    sa_family_t    sin_family;
    uint16_t       sin_port;
    uint32_t       sin_addr;
    char           sin_zero[8];
};

long syscall(long number, ...);

int crear_socket_tcp() {
    return syscall(SYS_socket, AF_INET, SOCK_STREAM, 0);
}

int preparar_servidor_tcp(int puerto) {
    int sock_fd = crear_socket_tcp();
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = ((puerto & 0xFF) << 8) | ((puerto >> 8) & 0xFF); // htons manual
    addr.sin_addr = INADDR_ANY;

    syscall(SYS_bind, sock_fd, &addr, sizeof(addr));
    syscall(SYS_listen, sock_fd, 10);

    return sock_fd;
}

int aceptar_conexion(int servidor_fd) {
    return syscall(SYS_accept, servidor_fd, 0, 0);
}

int conectar_a_servidor(const unsigned int ip, int puerto) {
    int sock_fd = crear_socket_tcp();
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = ((puerto & 0xFF) << 8) | ((puerto >> 8) & 0xFF); // htons manual
    addr.sin_addr = ip;

    syscall(SYS_connect, sock_fd, &addr, sizeof(addr));
    return sock_fd;
}

void cerrar_socket(int sock_fd) {
    syscall(SYS_close, sock_fd);
}