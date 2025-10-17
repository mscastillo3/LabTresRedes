/*
 * Archivo: publisher_quic.c
 * Descripcion: Publicador que envia eventos deportivos al broker usando msquic.
 *
 * LIBRERIAS UTILIZADAS Y JUSTIFICACION:
 *
 * 1. msquic.h
 *    - Por que: Facilita el establecimiento de conexiones QUIC seguras y manejo de streams bidireccionales.
 *    - Funciones usadas: MsQuicOpen(), MsQuic->RegistrationOpen(), MsQuic->ConfigurationOpen(),
 *      MsQuic->ConfigurationLoadCredential(), MsQuic->ConnectionOpen(), MsQuic->ConnectionStart(),
 *      MsQuic->StreamOpen(), MsQuic->StreamStart(), MsQuic->StreamSend().
 *    - Alternativa considerada: Implementar QUIC manualmente sobre UDP; descartado por complejidad y riesgo.
 *
 * 2. stdio.h (libreria estandar)
 *    - Por que: Lectura del archivo de eventos y logs en consola.
 *    - Funciones usadas: printf(), fprintf(), fgets(), fopen(), fclose().
 *
 * 3. stdlib.h (libreria estandar)
 *    - Por que: Manejo de memoria y conversion de argumentos numericos.
 *    - Funciones usadas: malloc(), free(), atoi(), exit().
 *
 * 4. string.h (libreria estandar)
 *    - Por que: Construccion de mensajes y sanitizacion de lineas leidas.
 *    - Funciones usadas: strlen(), strcpy(), strncpy(), strcspn(), memset().
 *
 * 5. time.h (libreria estandar)
 *    - Por que: Generacion de timestamps para los eventos publicados.
 *    - Funciones usadas: time(), localtime(), strftime().
 *
 * 6. windows.h
 *    - Por que: Eventos de sincronizacion (CreateEvent/WaitForSingleObject) y atomicos (Interlocked*),
 *      necesarios para coordinar envio asincrono con los callbacks de msquic.
 *    - Funciones usadas: CreateEventA(), SetEvent(), WaitForSingleObject(), CloseHandle(), InterlockedIncrement(),
 *      InterlockedDecrement().
 *    - Alternativa considerada: Implementar sincronizacion manual con busy-wait; descartado para evitar consumo excesivo de CPU.
 */

#include <msquic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define MESSAGE_MAX_LEN 512

static const QUIC_API_TABLE* MsQuic = NULL;
static HQUIC Registration = NULL;
static HQUIC Configuration = NULL;
static HQUIC Connection = NULL;
static HQUIC Stream = NULL;

static const char* const DEFAULT_ALPN = "sports-pubsub";

typedef struct SendContext {
    uint8_t* buffer;
    uint32_t length;
} SendContext;

typedef struct PublisherContext {
    HANDLE ConnectedEvent;
    HANDLE ConnectionShutdownEvent;
    HANDLE StreamShutdownEvent;
    volatile LONG OutstandingSends;
} PublisherContext;

static PublisherContext AppContext = { NULL, NULL, NULL, 0 };

static int InitializeEvents(void) {
    AppContext.ConnectedEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    AppContext.ConnectionShutdownEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    AppContext.StreamShutdownEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

    if (!AppContext.ConnectedEvent ||
        !AppContext.ConnectionShutdownEvent ||
        !AppContext.StreamShutdownEvent) {
        fprintf(stderr, "[PUBLISHER] No se pudieron crear eventos de sincronizacion.\n");
        return 0;
    }
    return 1;
}

static void DisposeEvents(void) {
    if (AppContext.ConnectedEvent) {
        CloseHandle(AppContext.ConnectedEvent);
        AppContext.ConnectedEvent = NULL;
    }
    if (AppContext.ConnectionShutdownEvent) {
        CloseHandle(AppContext.ConnectionShutdownEvent);
        AppContext.ConnectionShutdownEvent = NULL;
    }
    if (AppContext.StreamShutdownEvent) {
        CloseHandle(AppContext.StreamShutdownEvent);
        AppContext.StreamShutdownEvent = NULL;
    }
}

static uint8_t* DuplicateBytes(const char* source, size_t length) {
    uint8_t* copy = (uint8_t*)malloc(length);
    if (copy != NULL) {
        memcpy(copy, source, length);
    }
    return copy;
}

static QUIC_STATUS QueueSend(const char* text) {
    size_t length = strlen(text);
    if (length == 0) {
        return QUIC_STATUS_SUCCESS;
    }

    SendContext* ctx = (SendContext*)malloc(sizeof(SendContext));
    if (ctx == NULL) {
        return QUIC_STATUS_OUT_OF_MEMORY;
    }

    ctx->buffer = DuplicateBytes(text, length);
    if (ctx->buffer == NULL) {
        free(ctx);
        return QUIC_STATUS_OUT_OF_MEMORY;
    }
    ctx->length = (uint32_t)length;

    QUIC_BUFFER buffer;
    buffer.Buffer = ctx->buffer;
    buffer.Length = ctx->length;

    InterlockedIncrement(&AppContext.OutstandingSends);
    QUIC_STATUS status = MsQuic->StreamSend(Stream, &buffer, 1, QUIC_SEND_FLAG_NONE, ctx);
    if (QUIC_FAILED(status)) {
        InterlockedDecrement(&AppContext.OutstandingSends);
        free(ctx->buffer);
        free(ctx);
    }
    return status;
}

static void BuildTimestamp(char* destination, size_t capacity) {
    time_t now = time(NULL);
    struct tm* info = localtime(&now);
    if (info != NULL) {
        strftime(destination, capacity, "%H:%M:%S", info);
    } else {
        strncpy(destination, "00:00:00", capacity - 1);
        destination[capacity - 1] = '\0';
    }
}

static
QUIC_STATUS
PublisherStreamCallback(
    HQUIC stream,
    void* context,
    QUIC_STREAM_EVENT* event
    )
{
    (void)stream;
    (void)context;

    switch (event->Type) {
    case QUIC_STREAM_EVENT_SEND_COMPLETE: {
        SendContext* ctx = (SendContext*)event->SEND_COMPLETE.ClientContext;
        if (ctx != NULL) {
            free(ctx->buffer);
            free(ctx);
        }
        InterlockedDecrement(&AppContext.OutstandingSends);
        break;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        SetEvent(AppContext.StreamShutdownEvent);
        MsQuic->StreamClose(stream);
        Stream = NULL;
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static
QUIC_STATUS
PublisherConnectionCallback(
    HQUIC connection,
    void* context,
    QUIC_CONNECTION_EVENT* event
    )
{
    (void)connection;
    (void)context;

    switch (event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        printf("[PUBLISHER] Conexion QUIC establecida.\n");
        SetEvent(AppContext.ConnectedEvent);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        printf("[PUBLISHER] Conexion finalizada.\n");
        SetEvent(AppContext.ConnectionShutdownEvent);
        MsQuic->ConnectionClose(Connection);
        Connection = NULL;
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static int InitializeQuic(void) {
    QUIC_STATUS status = MsQuicOpen2(&MsQuic);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[PUBLISHER] MsQuicOpen fracaso (%u).\n", status);
        return 0;
    }

    QUIC_REGISTRATION_CONFIG regConfig = { "PublisherApp", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    status = MsQuic->RegistrationOpen(&regConfig, &Registration);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[PUBLISHER] RegistrationOpen fracaso (%u).\n", status);
        MsQuicClose(MsQuic);
        return 0;
    }

    QUIC_BUFFER alpn;
    alpn.Buffer = (uint8_t*)DEFAULT_ALPN;
    alpn.Length = (uint32_t)strlen(DEFAULT_ALPN);

    QUIC_SETTINGS settings;
    memset(&settings, 0, sizeof(settings));
    settings.IsSet.HandshakeIdleTimeoutMs = TRUE;
    settings.HandshakeIdleTimeoutMs = 600000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.IdleTimeoutMs = 600000;
    settings.IsSet.SendIdleTimeoutMs = TRUE;
    settings.SendIdleTimeoutMs = 600000;
    settings.IsSet.KeepAliveIntervalMs = TRUE;
    settings.KeepAliveIntervalMs = 15000;

    status = MsQuic->ConfigurationOpen(
        Registration,
        &alpn,
        1,
        &settings,
        sizeof(settings),
        NULL,
        &Configuration);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[PUBLISHER] ConfigurationOpen fracaso (%u).\n", status);
        MsQuic->RegistrationClose(Registration);
        MsQuicClose(MsQuic);
        return 0;
    }

    QUIC_CREDENTIAL_CONFIG credConfig;
    memset(&credConfig, 0, sizeof(credConfig));
    credConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
    credConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT |
                       QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

    status = MsQuic->ConfigurationLoadCredential(Configuration, &credConfig);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[PUBLISHER] ConfigurationLoadCredential fracaso (%u).\n", status);
        MsQuic->ConfigurationClose(Configuration);
        MsQuic->RegistrationClose(Registration);
        MsQuicClose(MsQuic);
        return 0;
    }
    return 1;
}

static void CleanupQuic(void) {
    if (Stream != NULL) {
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE, 0);
        MsQuic->StreamClose(Stream);
        Stream = NULL;
    }
    if (Connection != NULL) {
        MsQuic->ConnectionShutdown(Connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        MsQuic->ConnectionClose(Connection);
        Connection = NULL;
    }
    if (Configuration != NULL) {
        MsQuic->ConfigurationClose(Configuration);
        Configuration = NULL;
    }
    if (Registration != NULL) {
        MsQuic->RegistrationClose(Registration);
        Registration = NULL;
    }
    if (MsQuic != NULL) {
        MsQuicClose(MsQuic);
        MsQuic = NULL;
    }
}

static int StartConnection(const char* serverName, uint16_t port) {
    QUIC_STATUS status = MsQuic->ConnectionOpen(
        Registration,
        PublisherConnectionCallback,
        NULL,
        &Connection);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[PUBLISHER] ConnectionOpen fracaso (%u).\n", status);
        return 0;
    }

    status = MsQuic->ConnectionStart(
        Connection,
        Configuration,
        AF_UNSPEC,
        serverName,
        port);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[PUBLISHER] ConnectionStart fracaso (%u).\n", status);
        MsQuic->ConnectionClose(Connection);
        Connection = NULL;
        return 0;
    }
    return 1;
}

static int OpenStream(void) {
    QUIC_STATUS status = MsQuic->StreamOpen(
        Connection,
        QUIC_STREAM_OPEN_FLAG_NONE,
        PublisherStreamCallback,
        NULL,
        &Stream);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[PUBLISHER] StreamOpen fracaso (%u).\n", status);
        return 0;
    }

    status = MsQuic->StreamStart(
        Stream,
        QUIC_STREAM_START_FLAG_IMMEDIATE |
        QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[PUBLISHER] StreamStart fracaso (%u).\n", status);
        MsQuic->StreamClose(Stream);
        Stream = NULL;
        return 0;
    }
    return 1;
}

static int PublishEvents(const char* topic, const char* filePath) {
    FILE* file = fopen(filePath, "r");
    if (file == NULL) {
        fprintf(stderr, "[PUBLISHER] No se pudo abrir %s\n", filePath);
        return 0;
    }

    printf("[PUBLISHER] Publicando eventos de %s para el partido %s\n", filePath, topic);

    char line[MESSAGE_MAX_LEN];
    int lineNumber = 1;
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) {
            continue;
        }

        char timestamp[16];
        BuildTimestamp(timestamp, sizeof(timestamp));

        char outbound[MESSAGE_MAX_LEN];
        snprintf(outbound, sizeof(outbound), "PUBLISHER|%s|%s|%s", topic, timestamp, line);

        QUIC_STATUS status = QueueSend(outbound);
        if (QUIC_FAILED(status)) {
            fprintf(stderr, "[PUBLISHER] Error enviando linea %d (%u).\n", lineNumber, status);
            fclose(file);
            return 0;
        }
        printf("[PUBLISHER] Mensaje enviado: %s\n", outbound);
        ++lineNumber;
    }

    fclose(file);
    return 1;
}

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <IP_BROKER> <PUERTO> <TOPIC> <ARCHIVO_MENSAJES>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* brokerAddress = argv[1];
    int portValue = atoi(argv[2]);
    if (portValue <= 0 || portValue > 65535) {
        fprintf(stderr, "[PUBLISHER] Puerto invalido: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    if (!InitializeEvents()) {
        DisposeEvents();
        return EXIT_FAILURE;
    }

    if (!InitializeQuic()) {
        DisposeEvents();
        return EXIT_FAILURE;
    }

    if (!StartConnection(brokerAddress, (uint16_t)portValue)) {
        CleanupQuic();
        DisposeEvents();
        return EXIT_FAILURE;
    }

    DWORD waitResult = WaitForSingleObject(AppContext.ConnectedEvent, 15000);
    if (waitResult != WAIT_OBJECT_0) {
        fprintf(stderr, "[PUBLISHER] Timeout esperando la conexion.\n");
        CleanupQuic();
        DisposeEvents();
        return EXIT_FAILURE;
    }

    if (!OpenStream()) {
        CleanupQuic();
        DisposeEvents();
        return EXIT_FAILURE;
    }

    if (!PublishEvents(argv[3], argv[4])) {
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND, 0);
    } else {
        MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL, 0);
    }

    WaitForSingleObject(AppContext.StreamShutdownEvent, 15000);

    while (AppContext.OutstandingSends > 0) {
        Sleep(10);
    }

    MsQuic->ConnectionShutdown(Connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
    WaitForSingleObject(AppContext.ConnectionShutdownEvent, 5000);

    CleanupQuic();
    DisposeEvents();

    printf("[PUBLISHER] Finalizado.\n");
    return EXIT_SUCCESS;
}
