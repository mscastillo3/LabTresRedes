#ifndef TCP_UTILS_H
#define TCP_UTILS_H

int crear_socket();
int conectar_a(const char *ip, int puerto);
int enviar(int fd, const char *msg);
int recibir(int fd, char *buffer, int size);

#endif
