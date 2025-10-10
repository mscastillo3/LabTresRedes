#ifndef UTILS_TCP_H
#define UTILS_TCP_H

int crear_socket_tcp();
int preparar_servidor_tcp(int puerto);
int aceptar_conexion(int servidor_fd);
int conectar_a_servidor(const unsigned int ip, int puerto);
void cerrar_socket(int sock_fd);

#endif
