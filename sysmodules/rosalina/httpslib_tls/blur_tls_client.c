#include <stddef.h>
#include "mbedtls/ssl.h"

#define BLUR_TLS_CODE __attribute__((used))

typedef int (*BlurTlsRngFn)(void *, unsigned char *, size_t);
typedef int (*BlurTlsSendFn)(void *, const unsigned char *, size_t);
typedef int (*BlurTlsRecvFn)(void *, unsigned char *, size_t);

typedef struct
{
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
} BlurTlsClient;

extern void *PLUGIN_htps_TlsCalloc(size_t n, size_t size);
extern void PLUGIN_htps_TlsFree(void *p);

BLUR_TLS_CODE int PLUGIN_htps_TlsConnect(
    void **out,
    const char *host,
    void *bio,
    BlurTlsRngFn rng,
    void *rngCtx,
    BlurTlsSendFn sendFn,
    BlurTlsRecvFn recvFn)
{
    BlurTlsClient *client;
    int ret;

    if (!out || !host || !rng || !sendFn || !recvFn)
        return -1;

    *out = NULL;
    client = (BlurTlsClient *)PLUGIN_htps_TlsCalloc(1, sizeof(*client));
    if (!client)
        return -1;

    mbedtls_ssl_init(&client->ssl);
    mbedtls_ssl_config_init(&client->config);

    ret = mbedtls_ssl_config_defaults(
        &client->config,
        MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
        goto fail;

    mbedtls_ssl_conf_authmode(&client->config, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&client->config, rng, rngCtx);

    ret = mbedtls_ssl_setup(&client->ssl, &client->config);
    if (ret != 0)
        goto fail;

    ret = mbedtls_ssl_set_hostname(&client->ssl, host);
    if (ret != 0)
        goto fail;

    mbedtls_ssl_set_bio(&client->ssl, bio, sendFn, recvFn, NULL);

    do
    {
        ret = mbedtls_ssl_handshake(&client->ssl);
    }
    while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

    if (ret != 0)
        goto fail;

    *out = client;
    return 0;

fail:
    mbedtls_ssl_free(&client->ssl);
    mbedtls_ssl_config_free(&client->config);
    PLUGIN_htps_TlsFree(client);
    return ret;
}

BLUR_TLS_CODE int PLUGIN_htps_TlsWrite(void *opaque, const void *buffer, size_t size)
{
    BlurTlsClient *client = (BlurTlsClient *)opaque;
    const unsigned char *bytes = (const unsigned char *)buffer;
    size_t done = 0;

    while (done < size)
    {
        int ret = mbedtls_ssl_write(&client->ssl, bytes + done, size - done);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (ret <= 0)
            return ret ? ret : -1;
        done += (size_t)ret;
    }
    return 0;
}

BLUR_TLS_CODE int PLUGIN_htps_TlsRead(void *opaque, void *buffer, size_t size)
{
    BlurTlsClient *client = (BlurTlsClient *)opaque;

    for (;;)
    {
        int ret = mbedtls_ssl_read(&client->ssl, (unsigned char *)buffer, size);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
            return 0;
        return ret;
    }
}

BLUR_TLS_CODE void PLUGIN_htps_TlsClose(void *opaque)
{
    BlurTlsClient *client = (BlurTlsClient *)opaque;
    if (!client)
        return;
    mbedtls_ssl_free(&client->ssl);
    mbedtls_ssl_config_free(&client->config);
    PLUGIN_htps_TlsFree(client);
}