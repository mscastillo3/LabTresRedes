/*
 * Archivo: broker_quic.c
 * Descripcion: Broker QUIC para el sistema Publish/Subscribe de noticias deportivas.
 *
 * LIBRERIAS UTILIZADAS Y JUSTIFICACION:
 *
 * 1. msquic.h
 *    - Por que: Proporciona la API oficial de Microsoft para QUIC (RFC 9000), incluyendo manejo de transporte seguro,
 *      streams multiplexados y TLS 1.3 integrado.
 *    - Funciones usadas: MsQuicOpen(), MsQuicClose(), MsQuic->RegistrationOpen(), MsQuic->ConfigurationOpen(),
 *      MsQuic->ConfigurationLoadCredential(), MsQuic->ListenerOpen(), MsQuic->ListenerStart(),
 *      MsQuic->SetCallbackHandler(), MsQuic->ConnectionSetConfiguration(), MsQuic->StreamSend().
 *    - Alternativa considerada: Implementar QUIC manualmente sobre UDP; descartado por el alcance (>10k lineas) y riesgos.
 *
 * 2. stdio.h (libreria estandar)
 *    - Por que: Salida de diagnostico y mensajes informativos.
 *    - Funciones usadas: printf(), fprintf().
 *
 * 3. stdlib.h (libreria estandar)
 *    - Por que: Manejo de memoria dinamica y conversion de cadenas numericas.
 *    - Funciones usadas: malloc(), calloc(), free(), atoi(), exit().
 *
 * 4. string.h (libreria estandar)
 *    - Por que: Manipulacion de cadenas para procesar topics y mensajes.
 *    - Funciones usadas: strlen(), strcpy(), strncpy(), strcmp(), strtok(), memset().
 *
 * 5. windows.h
 *    - Por que: Necesario para sincronizacion con CRITICAL_SECTION utilizada al compartir la tabla de subscriptores
 *      entre callbacks concurrentes de msquic.
 *    - Funciones usadas: InitializeCriticalSection(), EnterCriticalSection(), LeaveCriticalSection(),
 *      DeleteCriticalSection(), Sleep().
 *    - Alternativa considerada: Implementar spinlocks manualmente; descartado para evitar condiciones de carrera.
 *
 * 6. wincrypt.h
 *    - Por que: Permite importar certificados PKCS#12 (PFX) via PFXImportCertStore y obtener PCCERT_CONTEXT requerido por msquic.
 *    - Funciones usadas: PFXImportCertStore(), CertEnumCertificatesInStore(), CertFreeCertificateContext(), CertCloseStore().
 *    - Alternativa considerada: Cargar certificados manualmente usando APIs de bajo nivel; descartado para aprovechar utilidades CryptoAPI ya disponibles.
 *
 * 7. winsock2.h
 *    - Por que: Conversiones de orden de bytes y definiciones de INADDR_ANY requeridas al configurar el listener.
 *    - Funciones usadas: htons(), htonl().
 *    - Alternativa considerada: Reimplementar conversiones de endianess; descartado para reducir errores.
 */

#include <msquic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <winsock2.h>

#ifndef PKCS12_ALLOW_EXPORT
#define PKCS12_ALLOW_EXPORT 0x00000002
#endif
#ifndef PKCS12_NO_PERSIST_KEY
#define PKCS12_NO_PERSIST_KEY 0x00000004
#endif
#ifndef PKCS12_ALWAYS_CNG_KSP
#define PKCS12_ALWAYS_CNG_KSP 0x00000080
#endif

#define MAX_SUBSCRIBERS 128
#define TOPIC_NAME_LEN 64
#define MESSAGE_MAX_LEN 512

typedef enum ClientType {
    CLIENT_UNKNOWN = 0,
    CLIENT_PUBLISHER,
    CLIENT_SUBSCRIBER
} ClientType;

typedef struct ClientContext {
    ClientType type;
    char topic[TOPIC_NAME_LEN];
    HQUIC connection;
    HQUIC activeStream;
    int subscribed;
} ClientContext;

typedef struct SubscriberEntry {
    int inUse;
    char topic[TOPIC_NAME_LEN];
    HQUIC connection;
    HQUIC stream;
    ClientContext* client;
} SubscriberEntry;

typedef struct StreamContext {
    ClientContext* client;
    char receiveBuffer[MESSAGE_MAX_LEN];
    size_t receiveLength;
} StreamContext;

typedef struct SendContext {
    uint8_t* buffer;
    uint32_t length;
} SendContext;

static const QUIC_API_TABLE* MsQuic = NULL;
static HQUIC Registration = NULL;
static HQUIC Configuration = NULL;
static HQUIC Listener = NULL;
static HCERTSTORE BrokerCertStore = NULL;
static PCERT_CONTEXT BrokerCertificate = NULL;

static SubscriberEntry Subscribers[MAX_SUBSCRIBERS];
static CRITICAL_SECTION SubscribersLock;

static const char* const DEFAULT_ALPN = "sports-pubsub";

static void RemoveSubscriberByClient(ClientContext* client);
static void RemoveSubscriberByStream(HQUIC stream);

static uint8_t* DuplicateBytes(const char* source, size_t length) {
    uint8_t* copy = (uint8_t*)malloc(length);
    if (copy != NULL) {
        memcpy(copy, source, length);
    }
    return copy;
}

static int ReadFileToBuffer(const char* path, uint8_t** buffer, uint32_t* length) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "[BROKER] No se pudo abrir el certificado PFX: %s\n", path);
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "[BROKER] Error al leer tamano del PFX.\n");
        fclose(file);
        return 0;
    }

    long fileSize = ftell(file);
    if (fileSize <= 0) {
        fprintf(stderr, "[BROKER] El certificado PFX parece vacio.\n");
        fclose(file);
        return 0;
    }
    rewind(file);

    uint8_t* data = (uint8_t*)malloc((size_t)fileSize);
    if (data == NULL) {
        fprintf(stderr, "[BROKER] Memoria insuficiente para cargar PFX.\n");
        fclose(file);
        return 0;
    }

    size_t read = fread(data, 1, (size_t)fileSize, file);
    fclose(file);

    if (read != (size_t)fileSize) {
        fprintf(stderr, "[BROKER] Error al leer el contenido completo del PFX.\n");
        free(data);
        return 0;
    }

    *buffer = data;
    *length = (uint32_t)read;
    return 1;
}

static PCCERT_CONTEXT ImportCertificateContext(
    const uint8_t* pfxBuffer,
    uint32_t length,
    const char* password)
{
    DATA_BLOB blob;
    blob.cbData = length;
    blob.pbData = (BYTE*)pfxBuffer;

    const WCHAR* widePassword = NULL;
    WCHAR* passwordStorage = NULL;

    if (password != NULL && password[0] != '\0') {
        int wideLength = MultiByteToWideChar(CP_UTF8, 0, password, -1, NULL, 0);
        if (wideLength <= 0) {
            return NULL;
        }
        passwordStorage = (WCHAR*)malloc((size_t)wideLength * sizeof(WCHAR));
        if (passwordStorage == NULL) {
            return NULL;
        }
        if (MultiByteToWideChar(CP_UTF8, 0, password, -1, passwordStorage, wideLength) <= 0) {
            free(passwordStorage);
            return NULL;
        }
        widePassword = passwordStorage;
    }

    HCERTSTORE store = PFXImportCertStore(
        &blob,
        widePassword,
        PKCS12_NO_PERSIST_KEY |
        PKCS12_ALLOW_EXPORT |
        PKCS12_ALWAYS_CNG_KSP);

    if (passwordStorage != NULL) {
        free(passwordStorage);
    }

    if (store == NULL) {
        fprintf(stderr, "[BROKER] PFXImportCertStore fallo (0x%lx).\n", GetLastError());
        return NULL;
    }

    PCCERT_CONTEXT context = CertEnumCertificatesInStore(store, NULL);
    if (context == NULL) {
        fprintf(stderr, "[BROKER] No se encontro certificado en el PFX (0x%lx).\n", GetLastError());
        CertCloseStore(store, 0);
        return NULL;
    }

    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE keyHandle = 0;
    DWORD keySpec = 0;
    BOOL mustFree = FALSE;
    if (!CryptAcquireCertificatePrivateKey(
            context,
            CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG,
            NULL,
            &keyHandle,
            &keySpec,
            &mustFree)) {
        fprintf(stderr, "[BROKER] CryptAcquireCertificatePrivateKey fallo (0x%lx).\n", GetLastError());
        CertFreeCertificateContext(context);
        CertCloseStore(store, 0);
        return NULL;
    }

    if (mustFree) {
        NCryptFreeObject(keyHandle);
    }

    BrokerCertStore = store;
    return context;
}

static QUIC_STATUS SendTextOnStream(HQUIC stream, const char* text) {
    size_t len = strlen(text);
    if (len == 0) {
        return QUIC_STATUS_SUCCESS;
    }

    SendContext* context = (SendContext*)malloc(sizeof(SendContext));
    if (context == NULL) {
        return QUIC_STATUS_OUT_OF_MEMORY;
    }

    context->buffer = DuplicateBytes(text, len);
    if (context->buffer == NULL) {
        free(context);
        return QUIC_STATUS_OUT_OF_MEMORY;
    }
    context->length = (uint32_t)len;

    QUIC_BUFFER buffer;
    buffer.Length = context->length;
    buffer.Buffer = context->buffer;

    QUIC_STATUS status =
        MsQuic->StreamSend(stream, &buffer, 1, QUIC_SEND_FLAG_NONE, context);
    if (QUIC_FAILED(status)) {
        free(context->buffer);
        free(context);
    }

    return status;
}

static void AddOrUpdateSubscriber(const char* topic, ClientContext* client, HQUIC stream) {
    EnterCriticalSection(&SubscribersLock);

    for (int i = 0; i < MAX_SUBSCRIBERS; ++i) {
        if (Subscribers[i].inUse &&
            Subscribers[i].client == client) {
            strncpy(Subscribers[i].topic, topic, TOPIC_NAME_LEN - 1);
            Subscribers[i].topic[TOPIC_NAME_LEN - 1] = '\0';
            Subscribers[i].stream = stream;
            LeaveCriticalSection(&SubscribersLock);
            return;
        }
    }

    for (int i = 0; i < MAX_SUBSCRIBERS; ++i) {
        if (!Subscribers[i].inUse) {
            Subscribers[i].inUse = 1;
            Subscribers[i].connection = client->connection;
            Subscribers[i].stream = stream;
            Subscribers[i].client = client;
            strncpy(Subscribers[i].topic, topic, TOPIC_NAME_LEN - 1);
            Subscribers[i].topic[TOPIC_NAME_LEN - 1] = '\0';
            LeaveCriticalSection(&SubscribersLock);
            return;
        }
    }

    LeaveCriticalSection(&SubscribersLock);
    fprintf(stderr, "[BROKER] Tabla de subscriptores llena, no se puede registrar %s.\n", topic);
}

static void BroadcastToTopic(const char* topic, const char* payload) {
    EnterCriticalSection(&SubscribersLock);

    for (int i = 0; i < MAX_SUBSCRIBERS; ++i) {
        if (Subscribers[i].inUse &&
            strcmp(Subscribers[i].topic, topic) == 0 &&
            Subscribers[i].stream != NULL) {
            QUIC_STATUS status = SendTextOnStream(Subscribers[i].stream, payload);
            if (QUIC_FAILED(status)) {
                fprintf(stderr, "[BROKER] Error enviando a subscriptor (%s). Se eliminaran sus datos.\n", topic);
                Subscribers[i].inUse = 0;
                Subscribers[i].stream = NULL;
                if (Subscribers[i].client != NULL) {
                    Subscribers[i].client->subscribed = 0;
                    Subscribers[i].client->activeStream = NULL;
                }
            }
        }
    }

    LeaveCriticalSection(&SubscribersLock);
}

static void RemoveSubscriberByStream(HQUIC stream) {
    EnterCriticalSection(&SubscribersLock);

    for (int i = 0; i < MAX_SUBSCRIBERS; ++i) {
        if (Subscribers[i].inUse && Subscribers[i].stream == stream) {
            Subscribers[i].inUse = 0;
            if (Subscribers[i].client != NULL) {
                Subscribers[i].client->subscribed = 0;
                Subscribers[i].client->activeStream = NULL;
            }
            Subscribers[i].stream = NULL;
            Subscribers[i].client = NULL;
        }
    }

    LeaveCriticalSection(&SubscribersLock);
}

static void RemoveSubscriberByClient(ClientContext* client) {
    EnterCriticalSection(&SubscribersLock);

    for (int i = 0; i < MAX_SUBSCRIBERS; ++i) {
        if (Subscribers[i].inUse &&
            Subscribers[i].client == client) {
            Subscribers[i].inUse = 0;
            Subscribers[i].stream = NULL;
            Subscribers[i].client = NULL;
        }
    }

    LeaveCriticalSection(&SubscribersLock);
}

static void ProcessPublisherMessage(ClientContext* client, const char* message) {
    (void)client;
    char working[MESSAGE_MAX_LEN];
    strncpy(working, message + 10, MESSAGE_MAX_LEN - 1);
    working[MESSAGE_MAX_LEN - 1] = '\0';

    char* topic = strtok(working, "|");
    char* timestamp = strtok(NULL, "|");
    char* rest = strtok(NULL, "");

    if (topic == NULL || timestamp == NULL || rest == NULL) {
        fprintf(stderr, "[BROKER] Mensaje de publisher malformado: %s\n", message);
        return;
    }

    char outbound[MESSAGE_MAX_LEN];
    snprintf(outbound, sizeof(outbound), "%s|%s", timestamp, rest);

    printf("[BROKER] Evento %s @ %s -> %s\n", topic, timestamp, rest);
    BroadcastToTopic(topic, outbound);
}

static void ProcessSubscriberMessage(ClientContext* client, StreamContext* streamContext, HQUIC stream, const char* message) {
    (void)streamContext;
    const char* topic = message + 11;
    if (strlen(topic) == 0) {
        fprintf(stderr, "[BROKER] Solicitud de suscripcion sin topic.\n");
        return;
    }

    client->type = CLIENT_SUBSCRIBER;
    strncpy(client->topic, topic, TOPIC_NAME_LEN - 1);
    client->topic[TOPIC_NAME_LEN - 1] = '\0';
    client->subscribed = 1;
    client->activeStream = stream;
    AddOrUpdateSubscriber(topic, client, stream);

    char ack[MESSAGE_MAX_LEN];
    snprintf(ack, sizeof(ack), "SUBSCRIBED|%s", topic);
    (void)SendTextOnStream(stream, ack);

    printf("[BROKER] Subscriptor registrado para %s\n", topic);
}

static void HandleReceivedData(HQUIC stream, StreamContext* ctx, const QUIC_STREAM_EVENT* event) {
    size_t total = ctx->receiveLength;
    for (uint32_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
        const QUIC_BUFFER* buffer = &event->RECEIVE.Buffers[i];
        size_t remaining = MESSAGE_MAX_LEN - 1 - total;
        size_t toCopy = buffer->Length < remaining ? buffer->Length : remaining;
        memcpy(ctx->receiveBuffer + total, buffer->Buffer, toCopy);
        total += toCopy;
        if (toCopy < buffer->Length) {
            fprintf(stderr, "[BROKER] Mensaje truncado (excede %d bytes).\n", MESSAGE_MAX_LEN);
            break;
        }
    }
    ctx->receiveBuffer[total] = '\0';
    ctx->receiveLength = 0;

    if (strncmp(ctx->receiveBuffer, "SUBSCRIBER|", 11) == 0) {
        ProcessSubscriberMessage(ctx->client, ctx, stream, ctx->receiveBuffer);
    } else if (strncmp(ctx->receiveBuffer, "PUBLISHER|", 10) == 0) {
        ctx->client->type = CLIENT_PUBLISHER;
        ProcessPublisherMessage(ctx->client, ctx->receiveBuffer);
    } else {
        fprintf(stderr, "[BROKER] Mensaje desconocido: %s\n", ctx->receiveBuffer);
    }
}

static
QUIC_STATUS
ServerStreamCallback(
    HQUIC stream,
    void* context,
    QUIC_STREAM_EVENT* event
    )
{
    StreamContext* streamContext = (StreamContext*)context;

    switch (event->Type) {
    case QUIC_STREAM_EVENT_RECEIVE:
        printf("[BROKER] Stream %p recibio %u buffers.\n",
               stream,
               event->RECEIVE.BufferCount);
        HandleReceivedData(stream, streamContext, event);
        break;

    case QUIC_STREAM_EVENT_SEND_COMPLETE: {
        SendContext* sendContext = (SendContext*)event->SEND_COMPLETE.ClientContext;
        if (sendContext != NULL) {
            free(sendContext->buffer);
            free(sendContext);
        }
        break;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        printf("[BROKER] Stream %p peer envio shutdown.\n", stream);
        break;

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        printf("[BROKER] Stream %p shutdown completo.\n", stream);
        RemoveSubscriberByStream(stream);
        MsQuic->StreamClose(stream);
        free(streamContext);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static
QUIC_STATUS
ServerConnectionCallback(
    HQUIC connection,
    void* context,
    QUIC_CONNECTION_EVENT* event
    )
{
    ClientContext* client = (ClientContext*)context;

    switch (event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        printf("[BROKER] Conexion establecida.\n");
        break;

    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
        printf("[BROKER] Conexion %p inicio stream entrante.\n", connection);
        StreamContext* streamContext = (StreamContext*)calloc(1, sizeof(StreamContext));
        if (streamContext == NULL) {
            fprintf(stderr, "[BROKER] Sin memoria para stream context.\n");
            MsQuic->StreamShutdown(
                event->PEER_STREAM_STARTED.Stream,
                QUIC_STREAM_SHUTDOWN_FLAG_ABORT_RECEIVE | QUIC_STREAM_SHUTDOWN_FLAG_ABORT_SEND,
                QUIC_STATUS_OUT_OF_MEMORY);
            break;
        }

        streamContext->client = client;
        MsQuic->SetCallbackHandler(
            event->PEER_STREAM_STARTED.Stream,
            (void*)ServerStreamCallback,
            streamContext);
        break;
    }

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        fprintf(stderr, "[BROKER] Transporte cerro conexion (0x%llx).\n",
                (unsigned long long)event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        printf("[BROKER] Peer cerro conexion.\n");
        break;

    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        RemoveSubscriberByClient(client);
        MsQuic->ConnectionClose(connection);
        free(client);
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

static
QUIC_STATUS
ServerListenerCallback(
    HQUIC listener,
    void* context,
    QUIC_LISTENER_EVENT* event
    )
{
    (void)listener;
    (void)context;

    switch (event->Type) {
    case QUIC_LISTENER_EVENT_NEW_CONNECTION: {
        ClientContext* client = (ClientContext*)calloc(1, sizeof(ClientContext));
        if (client == NULL) {
            fprintf(stderr, "[BROKER] No hay memoria para nuevo cliente.\n");
            return QUIC_STATUS_OUT_OF_MEMORY;
        }

        client->connection = event->NEW_CONNECTION.Connection;
        MsQuic->SetCallbackHandler(
            event->NEW_CONNECTION.Connection,
            (void*)ServerConnectionCallback,
            client);

        QUIC_STATUS status =
            MsQuic->ConnectionSetConfiguration(
                event->NEW_CONNECTION.Connection,
                Configuration);

        if (QUIC_FAILED(status)) {
            fprintf(stderr, "[BROKER] No se pudo asociar configuracion (%u).\n", status);
            MsQuic->ConnectionClose(event->NEW_CONNECTION.Connection);
            free(client);
            return status;
        }

        break;
    }

    case QUIC_LISTENER_EVENT_STOP_COMPLETE:
        printf("[BROKER] Listener detenido.\n");
        break;

    default:
        break;
    }

    return QUIC_STATUS_SUCCESS;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <PUERTO> <RUTA_CERTIFICADO_PFX> <PASSWORD_PFX>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s 5000 broker_dev.pfx PfxStrongPassword\n", argv[0]);
        return EXIT_FAILURE;
    }

    int portValue = atoi(argv[1]);
    if (portValue <= 0 || portValue > 65535) {
        fprintf(stderr, "[BROKER] Puerto invalido: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    InitializeCriticalSection(&SubscribersLock);

    QUIC_STATUS status = MsQuicOpen2(&MsQuic);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[BROKER] MsQuicOpen fracaso (%u).\n", status);
        DeleteCriticalSection(&SubscribersLock);
        return EXIT_FAILURE;
    }

    QUIC_REGISTRATION_CONFIG regConfig = { "BrokerApp", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    status = MsQuic->RegistrationOpen(&regConfig, &Registration);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[BROKER] RegistrationOpen fracaso (%u).\n", status);
        MsQuicClose(MsQuic);
        DeleteCriticalSection(&SubscribersLock);
        return EXIT_FAILURE;
    }

    QUIC_BUFFER alpn;
    alpn.Buffer = (uint8_t*)DEFAULT_ALPN;
    alpn.Length = (uint32_t)strlen(DEFAULT_ALPN);

    QUIC_SETTINGS settings;
    memset(&settings, 0, sizeof(settings));
    settings.IsSet.HandshakeIdleTimeoutMs = TRUE;
    settings.HandshakeIdleTimeoutMs = 600000;
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.IdleTimeoutMs = 600000; /* 10 minutos */
    settings.IsSet.SendIdleTimeoutMs = TRUE;
    settings.SendIdleTimeoutMs = 600000;
    settings.IsSet.KeepAliveIntervalMs = TRUE;
    settings.KeepAliveIntervalMs = 15000; /* keep-alive cada 15s */

    status = MsQuic->ConfigurationOpen(
        Registration,
        &alpn,
        1,
        &settings,
        sizeof(settings),
        NULL,
        &Configuration);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[BROKER] ConfigurationOpen fracaso (%u).\n", status);
        MsQuic->RegistrationClose(Registration);
        MsQuicClose(MsQuic);
        DeleteCriticalSection(&SubscribersLock);
        return EXIT_FAILURE;
    }

    uint8_t* pfxBuffer = NULL;
    uint32_t pfxLength = 0;
    if (!ReadFileToBuffer(argv[2], &pfxBuffer, &pfxLength)) {
        MsQuic->ConfigurationClose(Configuration);
        MsQuic->RegistrationClose(Registration);
        MsQuicClose(MsQuic);
        DeleteCriticalSection(&SubscribersLock);
        return EXIT_FAILURE;
    }

    BrokerCertificate = (PCERT_CONTEXT)ImportCertificateContext(pfxBuffer, pfxLength, argv[3]);
    free(pfxBuffer);
    if (BrokerCertificate == NULL) {
        MsQuic->ConfigurationClose(Configuration);
        MsQuic->RegistrationClose(Registration);
        MsQuicClose(MsQuic);
        DeleteCriticalSection(&SubscribersLock);
        return EXIT_FAILURE;
    }

    QUIC_CREDENTIAL_CONFIG credConfig;
    memset(&credConfig, 0, sizeof(credConfig));
    credConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
    credConfig.CertificateContext = (QUIC_CERTIFICATE*)BrokerCertificate;
    credConfig.Flags = QUIC_CREDENTIAL_FLAG_NONE;

    status = MsQuic->ConfigurationLoadCredential(Configuration, &credConfig);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[BROKER] ConfigurationLoadCredential fracaso (%u).\n", status);
        CertFreeCertificateContext(BrokerCertificate);
        BrokerCertificate = NULL;
        if (BrokerCertStore != NULL) {
            CertCloseStore(BrokerCertStore, 0);
            BrokerCertStore = NULL;
        }
        MsQuic->ConfigurationClose(Configuration);
        MsQuic->RegistrationClose(Registration);
        MsQuicClose(MsQuic);
        DeleteCriticalSection(&SubscribersLock);
        return EXIT_FAILURE;
    }

    status = MsQuic->ListenerOpen(
        Registration,
        ServerListenerCallback,
        NULL,
        &Listener);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[BROKER] ListenerOpen fracaso (%u).\n", status);
        MsQuic->ConfigurationClose(Configuration);
        MsQuic->RegistrationClose(Registration);
        MsQuicClose(MsQuic);
        DeleteCriticalSection(&SubscribersLock);
        return EXIT_FAILURE;
    }

    QUIC_ADDR address;
    memset(&address, 0, sizeof(address));
    address.Ipv4.sin_family = AF_INET;
    address.Ipv4.sin_port = htons((uint16_t)portValue);
    address.Ipv4.sin_addr.s_addr = htonl(INADDR_ANY);

    status = MsQuic->ListenerStart(
        Listener,
        &alpn,
        1,
        &address);
    if (QUIC_FAILED(status)) {
        fprintf(stderr, "[BROKER] ListenerStart fracaso (%u).\n", status);
        MsQuic->ListenerClose(Listener);
        MsQuic->ConfigurationClose(Configuration);
        MsQuic->RegistrationClose(Registration);
        MsQuicClose(MsQuic);
        DeleteCriticalSection(&SubscribersLock);
        return EXIT_FAILURE;
    }

    printf("[BROKER] Escuchando en puerto %d (ALPN: %s).\n", portValue, DEFAULT_ALPN);
    printf("[BROKER] Presiona ENTER para detener el broker.\n");
    (void)getchar();

    MsQuic->ListenerStop(Listener);
    MsQuic->ListenerClose(Listener);
    MsQuic->ConfigurationClose(Configuration);
    MsQuic->RegistrationClose(Registration);
    MsQuicClose(MsQuic);
    if (BrokerCertificate != NULL) {
        CertFreeCertificateContext(BrokerCertificate);
        BrokerCertificate = NULL;
    }
    if (BrokerCertStore != NULL) {
        CertCloseStore(BrokerCertStore, 0);
        BrokerCertStore = NULL;
    }
    DeleteCriticalSection(&SubscribersLock);

    printf("[BROKER] Finalizado correctamente.\n");
    return EXIT_SUCCESS;
}
