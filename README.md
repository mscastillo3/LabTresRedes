# LabTresRedes

## Ejecución TCP

### Requisitos 
* Sistema operativo Linux
* Compilador GCC (Correr los comandos: sudo apt update, sudo apt install build-essential)

### Compilación 

En caso de modificaciones al codigo fuente  se deben recompilar los programas. Ubíquese en la carpeta del proyecto (/TCP) y ejecute el siguiente comando en la terminal:

* gcc broker_tcp.c -o broker_tcp.exe -lws2_32 
* gcc subscriber_tcp.c -o subscriber_tcp.exe -lws2_32 
* gcc publisher_tcp.c -o publisher_tcp.exe -lws2_32 

### Ejecución del protocolo 

Para ejecutar el protocolo, abra cinco ventanas del terminal (una por programa) y ejecútelos en el siguiente orden:

1. .\broker_tcp.exe
2. .\subscriber_tcp.exe 1
3. .\subscriber_tcp.exe 2
4. .\publisher_tcp.exe Partido1.txt 1
5. .\publisher_tcp.exe Partido2.txt 2

Con esto generas dos subscriptores, dos publicadores y un broker si se quieren generar más se coloca el comando para crear uno nuevo.

## Ejecución UDP 

### Requisitos 
* Sistema operativo Windows
* Compilador GCC (Puede instalarlo utilizando este video https://www.youtube.com/watch?v=ry8V7N4diyc&t=24s)

### Compilación 

En caso de modificaciones al codigo fuente  se deben recompilar los programas. Ubíquese en la carpeta del proyecto (/UDP) y ejecute los siguientes comandos en la terminal:

* gcc broker_udp.c -o broker_udp.exe -lws2_32 (Compilar  el Broker)
* gcc subscriber_udp.c -o subscriber_udp.exe -lws2_32 (Compilar  el Subscriber)
* gcc publisher_udp.c -o publisher_udp.exe -lws2_32 (Compilar  el Publisher)

### Ejecución del protocolo (En la misma maquina)

Para ejecutar el protocolo en la misma maquina, abra cinco ventanas del terminal (una por programa) y ejecútelos en el siguiente orden:

1. .\broker_udp.exe 5000 (Broker) 
2. .\subscriber_udp.exe 127.0.0.1 5000 "Equipo A vs Equipo B" (Subscriber 1)
3. .\subscriber_udp.exe 127.0.0.1 5000 "Equipo C vs Equipo D" (Subscriber 2)
4. .\publisher_udp.exe 127.0.0.1 5000 "Equipo A vs Equipo B" Partido1.txt (Publisher 1)
5. .\publisher_udp.exe 127.0.0.1 5000 "Equipo C vs Equipo D" Partido2.txt (Publisher 2)

### Ejecucion del protocolo (En diferentes maquinas)

Para ejecutar el protocolo en diferentes maquinas. Primero debe obtener la IP local del computador donde se ejecutará el Broker. Posteriormente abra cinco ventanas del terminal (una por programa) y ejecútelos en el siguiente orden:

1. .\broker_udp.exe 5000 (Broker) 
2. .\subscriber_udp.exe (Reemplazar por la IP del Broker) 5000 "Equipo A vs Equipo B" (Subscriber 1)
3. .\subscriber_udp.exe (Reemplazar por la IP del Broker) 5000 "Equipo C vs Equipo D" (Subscriber 2)
4. .\publisher_udp.exe (Reemplazar por la IP del Broker) 5000 "Equipo A vs Equipo B" Partido1.txt (Publisher 1)
5. .\publisher_udp.exe (Reemplazar por la IP del Broker) 5000 "Equipo C vs Equipo D" Partido2.txt (Publisher 2)



