/*
 * Archivo: subscriber_quic.c
 * Descripcion: Cliente subscriptor que recibe eventos via QUIC desde el broker.
 *
 * LIBRERIAS UTILIZADAS Y JUSTIFICACION:
 *
 * 1. msquic.h
 *    - Por que: Permite establecer conexiones QUIC seguras y recibir datos por streams.
 *    - Funciones usadas: MsQuicOpen(), MsQuic->RegistrationOpen(), MsQuic->ConfigurationOpen(),
 *      MsQuic->ConfigurationLoadCredential(), MsQuic->ConnectionOpen(), MsQuic->ConnectionStart(),
 *      MsQuic->StreamOpen(), MsQuic->StreamStart(), MsQuic->StreamSend().
 *    - Alternativa considerada: Implementar QUIC manualmente; descartado por complejidad y cumplimiento de RFC.
 *
 * 2. stdio.h (libreria estandar)
 *    - Por que: Mostrar eventos recibidos y mensajes informativos.
 *    - Funciones usadas: printf(), fprintf().
 *
 * 3. stdlib.h (libreria estandar)
 *    - Por que: Manejar memoria y conversion de argumentos.
 *    - Funciones usadas: malloc(), free(), atoi(), exit().
 *
 * 4. string.h (libreria estandar)
 *    - Por que: Construccion del mensaje de suscripcion y buffers de recepcion.
 *    - Funciones usadas: strlen(), strcpy(), strncpy(), memset().
 *
 * 5. windows.h
 *    - Por que: Eventos de sincronizacion necesarios para coordinar el ciclo de vida con callbacks asincronos.
 *    - Funciones usadas: CreateEventA(), WaitForSingleObject(), SetEvent(), CloseHandle(), InterlockedIncrement(),
 *      InterlockedDecrement().
 *    - Alternativa considerada: Esperas activas; descartado por consumo innecesario de CPU.
 */

#include <msquic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

typedef struct SubscriberContext {
    HANDLE ConnectedEvent;
    HANDLE StreamReadyEvent;
    HANDLE ShutdownEvent;
    volatile LONG OutstandingSends;
    char Topic[MESSAGE_MAX_LEN];
} SubscriberContext;

static SubscriberContext AppContext = { NULL, NULL, NULL, 0, {0} };

static int InitializeEvents(void) {
    AppContext.ConnectedEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    AppContext.StreamReadyEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    AppContext.ShutdownEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

    if (!AppContext.ConnectedEvent ||
        !AppContext.StreamReadyEvent ||
        !AppContext.ShutdownEvent) {
        fprintf(stderr, "[SUBSCRIBER] No se pudieron crear eventos de sincronizacion.\n");
        return 0;
    }
    return 1;
}

static void DisposeEvents(void) {
    if (AppContext.ConnectedEvent) {
        CloseHandle(AppContext.ConnectedEvent);
        AppContext.ConnectedEvent = NULL;
    }
    if (AppContext.StreamReadyEvent) {
        CloseHandle(AppContext.StreamReadyEvent);
        AppContext.StreamReadyEvent = NULL;
    }
    if (AppContext.ShutdownEvent) {
        CloseHandle(AppContext.ShutdownEvent);
        AppContext.ShutdownEvent = NULL;
    }
}

static uint8_t* DuplicateBytes(const char* source, size_t length) {
    uint8_t* copy = (uint8_t*)malloc(length);
    if (copy != NULL) {
        memcpy(copy, source, length);
    }
    return copy;
}

static QUIC_STATUS SendSubscription(const char* topic) {
    char message[MESSAGE_MAX_LEN];
    snprintf(message, sizeof(message), "SUBSCRIBER|%s", topic);

    size_t length = strlen(message);
    if (length == 0) {
        return QUIC_STATUS_INVALID_PARAMETER;
    }

    SendContext* ctx = (SendContext*)malloc(sizeof(SendContext));
    if (ctx == NULL) {
        return QUIC_STATUS_OUT_OF_MEMORY;
    }

    ctx->buffer = DuplicateBytes(message, length);
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

static void PrintReceive(const QUIC_STREAM_EVENT* event) {
    char message[MESSAGE_MAX_LEN];
    size_t offset = 0;

    for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
        const QUIC_BUFFER* buffer = &event->RECEIVE.Buffers[i];
        size_t remaining = MESSAGE_MAX_LEN - 1 - offset;
        size_t toCopy = buffer->Length < remaining ? buffer->Length : remaining;
        memcpy(message + offset, buffer->Buffer, toCopy);
        offset += toCopy;
        if (toCopy < buffer->Length) {
            fprintf(stderr, "[SUBSCRIBER] Mensaje entrante truncado.\n");
            break;
        }
    }

    message[offset] = '\0';
    if (offset > 0) {
        printf("[SUBSCRIBER] Evento recibido (%s): %s\n", AppContext.Topic, message);
    }
}

static
QUIC_STATUS
SubscriberStreamCallback(
    HQUIC stream,
    void* context,
    QUIC_STREAM_EVENT* event
    )
{
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

    case QUIC_STREAM_EVENT_RECEIVE:
        PrintReceive(event);
        break;

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        printf("[SUBSCRIBER] El servidor cerro su flujo de envio.\n");
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        printf("[SUBSCRIBER] Stream cerrado.\n");
        MsQuic->StreamClose(stream);
        Stream = NULL;
        SetEvent(AppContext.ShutdownEvent);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static
QUIC_STATUS
SubscriberConnectionCallback(
    HQUIC connection,
    void* context,
    QUIC_CONNECTION_EVENT* event
    )
{
    (void)connection;
    (void)context;

    switch (event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        printf("[SUBSCRIBER] Conexion QUIC establecida.\n");
        SetEvent(AppContext.ConnectedEvent);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        printf("[SUBSCRIBER] Transporte inicio shutdown (0x%llx).\n",
               (unsigned long long)event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        printf("[SUBSCRIBER] Peer inicio shutdown voluntario.\n");
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        printf("[SUBSCRIBER] Conexion finalizada.\n");
        MsQuic->ConnectionClose(Connection);
        Connection = NULL;
        SetEvent(AppContext.ShutdownEvent);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static int InitializeQuic(void) {
    QUIC_STATUS status = MsQuicOpen2(&MsQuic);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[SUBSCRIBER] MsQuicOpen fracaso (%u).\n", status);
        return 0;
    }

    QUIC_REGISTRATION_CONFIG regConfig = { "SubscriberApp", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    status = MsQuic->RegistrationOpen(&regConfig, &Registration);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[SUBSCRIBER] RegistrationOpen fracaso (%u).\n", status);
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
        fprintf(stderr, "[SUBSCRIBER] ConfigurationOpen fracaso (%u).\n", status);
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
        fprintf(stderr, "[SUBSCRIBER] ConfigurationLoadCredential fracaso (%u).\n", status);
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
        SubscriberConnectionCallback,
        NULL,
        &Connection);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[SUBSCRIBER] ConnectionOpen fracaso (%u).\n", status);
        return 0;
    }

    status = MsQuic->ConnectionStart(
        Connection,
        Configuration,
        AF_UNSPEC,
        serverName,
        port);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[SUBSCRIBER] ConnectionStart fracaso (%u).\n", status);
        MsQuic->ConnectionClose(Connection);
        Connection = NULL;
        return 0;
    }

    return 1;
}

static int OpenStreamAndSubscribe(const char* topic) {
    QUIC_STATUS status = MsQuic->StreamOpen(
        Connection,
        QUIC_STREAM_OPEN_FLAG_NONE,
        SubscriberStreamCallback,
        NULL,
        &Stream);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[SUBSCRIBER] StreamOpen fracaso (%u).\n", status);
        return 0;
    }

    status = MsQuic->StreamStart(
        Stream,
        QUIC_STREAM_START_FLAG_IMMEDIATE |
        QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[SUBSCRIBER] StreamStart fracaso (%u).\n", status);
        MsQuic->StreamClose(Stream);
        Stream = NULL;
        return 0;
    }

    QUIC_STATUS sendStatus = SendSubscription(topic);
    if (QUIC_FAILED(sendStatus)) {
        fprintf(stderr, "[SUBSCRIBER] No se logro enviar la suscripcion (%u).\n", sendStatus);
        MsQuic->StreamShutdown(
            Stream,
            QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE,
            sendStatus);
        MsQuic->StreamClose(Stream);
        Stream = NULL;
        return 0;
    }

    printf("[SUBSCRIBER] Suscripcion enviada para %s.\n", topic);
    SetEvent(AppContext.StreamReadyEvent);
    return 1;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <IP_BROKER> <PUERTO> <TOPIC>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* brokerAddress = argv[1];
    int portValue = atoi(argv[2]);
    if (portValue <= 0 || portValue > 65535) {
        fprintf(stderr, "[SUBSCRIBER] Puerto invalido: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    if (strlen(argv[3]) >= sizeof(AppContext.Topic)) {
        fprintf(stderr, "[SUBSCRIBER] Topic demasiado largo.\n");
        return EXIT_FAILURE;
    }
    strcpy(AppContext.Topic, argv[3]);

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
        fprintf(stderr, "[SUBSCRIBER] Timeout esperando conexion.\n");
        CleanupQuic();
        DisposeEvents();
        return EXIT_FAILURE;
    }

    if (!OpenStreamAndSubscribe(AppContext.Topic)) {
        CleanupQuic();
        DisposeEvents();
        return EXIT_FAILURE;
    }

    printf("[SUBSCRIBER] Esperando eventos...\n");
    DWORD shutdownWait = WaitForSingleObject(AppContext.ShutdownEvent, INFINITE);
    if (shutdownWait == WAIT_OBJECT_0) {
        printf("[SUBSCRIBER] ShutdownEvent recibido, limpiando.\n");
    } else if (shutdownWait == WAIT_FAILED) {
        DWORD err = GetLastError();
        fprintf(stderr, "[SUBSCRIBER] WaitForSingleObject fallo (0x%lx).\n", err);
    } else if (shutdownWait == WAIT_ABANDONED) {
        printf("[SUBSCRIBER] ShutdownEvent abandonado; continuando cierre.\n");
    }

    while (AppContext.OutstandingSends > 0) {
        Sleep(5);
    }

    CleanupQuic();
    DisposeEvents();

    printf("[SUBSCRIBER] Finalizado.\n");
    return EXIT_SUCCESS;
}
