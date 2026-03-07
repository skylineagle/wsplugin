#ifndef TESTS_INCLUDE_FAKE_LWS_TEST_API_H
#define TESTS_INCLUDE_FAKE_LWS_TEST_API_H

#include <stddef.h>
#include "libwebsockets.h"

typedef struct {
    int port;
    int options;
    void *user;
    const struct lws_protocols *protocols;
} FakeLwsContextInfoSnapshot;

typedef struct {
    struct lws_context *context;
    char address[256];
    int port;
    char path[512];
    char host[256];
    char origin[256];
    char protocol[128];
    int ssl_connection;
} FakeLwsConnectInfoSnapshot;

void fake_lws_reset(void);
void fake_lws_set_create_context_fail(int should_fail);
void fake_lws_set_client_connect_fail(int should_fail);
void fake_lws_set_auto_callback(enum lws_callback_reasons reason, int enabled);
void fake_lws_set_auto_callback_detail(const char *detail);
void fake_lws_set_remaining_packet_payload(size_t remaining);
void fake_lws_set_final_fragment(int is_final);
void fake_lws_queue_write_result(int result);

FakeLwsContextInfoSnapshot fake_lws_get_last_context_info(void);
FakeLwsConnectInfoSnapshot fake_lws_get_last_connect_info(void);

int fake_lws_get_service_call_count(void);
int fake_lws_get_cancel_service_call_count(void);
int fake_lws_get_callback_on_writable_call_count(void);
int fake_lws_get_destroy_call_count(void);
int fake_lws_get_write_call_count(void);
size_t fake_lws_get_last_write_length(void);
const unsigned char *fake_lws_get_last_write_bytes(size_t *len);
struct lws_context *fake_lws_get_last_context(void);
struct lws *fake_lws_get_last_wsi(void);
void *fake_lws_get_session_data(struct lws *wsi);

struct lws *fake_lws_create_wsi(struct lws_context *context);
void fake_lws_trigger_callback(struct lws *wsi, enum lws_callback_reasons reason, void *in, size_t len);
void fake_lws_trigger_protocol_callback(struct lws_context *context, enum lws_callback_reasons reason, void *in, size_t len);

#endif
