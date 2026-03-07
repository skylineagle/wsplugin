#ifndef TESTS_INCLUDE_LIBWEBSOCKETS_H
#define TESTS_INCLUDE_LIBWEBSOCKETS_H

#include <stddef.h>
#include <stdint.h>

struct lws_context;
struct lws;

enum lws_callback_reasons {
    LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION = 1,
    LWS_CALLBACK_FILTER_NETWORK_CONNECTION,
    LWS_CALLBACK_ESTABLISHED,
    LWS_CALLBACK_CLIENT_ESTABLISHED,
    LWS_CALLBACK_RECEIVE,
    LWS_CALLBACK_CLIENT_RECEIVE,
    LWS_CALLBACK_CLIENT_CONNECTION_ERROR,
    LWS_CALLBACK_CLOSED,
    LWS_CALLBACK_CLIENT_CLOSED,
    LWS_CALLBACK_EVENT_WAIT_CANCELLED,
    LWS_CALLBACK_SERVER_WRITEABLE,
    LWS_CALLBACK_CLIENT_WRITEABLE,
};

enum lws_write_protocol {
    LWS_WRITE_BINARY = 1,
};

struct lws_protocols {
    const char *name;
    int (*callback)(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
    size_t per_session_data_size;
    size_t rx_buffer_size;
    unsigned int id;
    void *user;
    size_t tx_packet_size;
};

struct lws_context_creation_info {
    const struct lws_protocols *protocols;
    void *user;
    int options;
    int port;
};

struct lws_client_connect_info {
    struct lws_context *context;
    const char *address;
    int port;
    const char *path;
    const char *host;
    const char *origin;
    const char *protocol;
    int ssl_connection;
};

#define LWS_PROTOCOL_LIST_TERM { NULL, NULL, 0, 0, 0, NULL, 0 }
#define LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT 1
#define CONTEXT_PORT_NO_LISTEN (-1)
#define LCCSCF_USE_SSL 2
#define LWS_PRE 16

struct lws_context *lws_create_context(struct lws_context_creation_info *info);
void lws_context_destroy(struct lws_context *context);
void *lws_context_user(struct lws_context *context);
struct lws_context *lws_get_context(struct lws *wsi);
struct lws *lws_client_connect_via_info(const struct lws_client_connect_info *info);
size_t lws_remaining_packet_payload(struct lws *wsi);
int lws_is_final_fragment(struct lws *wsi);
int lws_service(struct lws_context *context, int timeout_ms);
void lws_cancel_service(struct lws_context *context);
void lws_callback_on_writable(struct lws *wsi);
int lws_write(struct lws *wsi, unsigned char *buf, size_t len, enum lws_write_protocol protocol);

#endif
