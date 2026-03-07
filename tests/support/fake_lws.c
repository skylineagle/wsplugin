#include <stdlib.h>
#include <string.h>

#include "fake_lws_test_api.h"

typedef struct _FakeWsiNode {
    struct lws *wsi;
    struct _FakeWsiNode *next;
} FakeWsiNode;

struct lws_context {
    const struct lws_protocols *protocols;
    void *user;
    int options;
    int port;
    FakeWsiNode *wsis;
    struct lws_context *next;
};

struct lws {
    struct lws_context *context;
    void *session_data;
};

typedef struct {
    int create_context_fail;
    int client_connect_fail;
    int auto_callback_enabled;
    enum lws_callback_reasons auto_callback_reason;
    char auto_callback_detail[128];
    size_t remaining_packet_payload;
    int final_fragment;
    int service_call_count;
    int cancel_service_call_count;
    int callback_on_writable_call_count;
    int destroy_call_count;
    int write_call_count;
    int queued_write_results[32];
    size_t queued_write_result_count;
    unsigned char *last_write_bytes;
    size_t last_write_length;
    FakeLwsContextInfoSnapshot last_context_info;
    FakeLwsConnectInfoSnapshot last_connect_info;
    struct lws_context *contexts;
    struct lws_context *last_context;
    struct lws *last_wsi;
} FakeLwsState;

static FakeLwsState state;

static void
fake_lws_copy_string(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0)
        return;

    if (!src) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static void
fake_lws_free_context(struct lws_context *context)
{
    FakeWsiNode *node = context->wsis;

    while (node) {
        FakeWsiNode *next = node->next;
        free(node->wsi->session_data);
        free(node->wsi);
        free(node);
        node = next;
    }

    free(context);
}

void
fake_lws_reset(void)
{
    struct lws_context *context = state.contexts;

    while (context) {
        struct lws_context *next = context->next;
        fake_lws_free_context(context);
        context = next;
    }

    free(state.last_write_bytes);
    memset(&state, 0, sizeof(state));
    state.final_fragment = 1;
}

void
fake_lws_set_create_context_fail(int should_fail)
{
    state.create_context_fail = should_fail;
}

void
fake_lws_set_client_connect_fail(int should_fail)
{
    state.client_connect_fail = should_fail;
}

void
fake_lws_set_auto_callback(enum lws_callback_reasons reason, int enabled)
{
    state.auto_callback_reason = reason;
    state.auto_callback_enabled = enabled;
}

void
fake_lws_set_auto_callback_detail(const char *detail)
{
    fake_lws_copy_string(state.auto_callback_detail, sizeof(state.auto_callback_detail), detail);
}

void
fake_lws_set_remaining_packet_payload(size_t remaining)
{
    state.remaining_packet_payload = remaining;
}

void
fake_lws_set_final_fragment(int is_final)
{
    state.final_fragment = is_final;
}

void
fake_lws_queue_write_result(int result)
{
    if (state.queued_write_result_count >= 32)
        return;

    state.queued_write_results[state.queued_write_result_count++] = result;
}

FakeLwsContextInfoSnapshot
fake_lws_get_last_context_info(void)
{
    return state.last_context_info;
}

FakeLwsConnectInfoSnapshot
fake_lws_get_last_connect_info(void)
{
    return state.last_connect_info;
}

int
fake_lws_get_service_call_count(void)
{
    return state.service_call_count;
}

int
fake_lws_get_cancel_service_call_count(void)
{
    return state.cancel_service_call_count;
}

int
fake_lws_get_callback_on_writable_call_count(void)
{
    return state.callback_on_writable_call_count;
}

int
fake_lws_get_destroy_call_count(void)
{
    return state.destroy_call_count;
}

int
fake_lws_get_write_call_count(void)
{
    return state.write_call_count;
}

size_t
fake_lws_get_last_write_length(void)
{
    return state.last_write_length;
}

const unsigned char *
fake_lws_get_last_write_bytes(size_t *len)
{
    if (len)
        *len = state.last_write_length;

    return state.last_write_bytes;
}

struct lws_context *
fake_lws_get_last_context(void)
{
    return state.last_context;
}

struct lws *
fake_lws_get_last_wsi(void)
{
    return state.last_wsi;
}

void *
fake_lws_get_session_data(struct lws *wsi)
{
    if (!wsi)
        return NULL;

    return wsi->session_data;
}

struct lws *
fake_lws_create_wsi(struct lws_context *context)
{
    FakeWsiNode *node;
    struct lws *wsi;

    if (!context)
        return NULL;

    wsi = calloc(1, sizeof(*wsi));
    node = calloc(1, sizeof(*node));

    if (!wsi || !node) {
        free(wsi);
        free(node);
        return NULL;
    }

    wsi->context = context;

    if (context->protocols && context->protocols[0].callback && context->protocols[0].per_session_data_size > 0)
        wsi->session_data = calloc(1, context->protocols[0].per_session_data_size);

    node->wsi = wsi;
    node->next = context->wsis;
    context->wsis = node;

    state.last_wsi = wsi;

    return wsi;
}

void
fake_lws_trigger_callback(struct lws *wsi, enum lws_callback_reasons reason, void *in, size_t len)
{
    const struct lws_protocols *protocols;

    if (!wsi || !wsi->context)
        return;

    protocols = wsi->context->protocols;
    if (!protocols || !protocols[0].callback)
        return;

    protocols[0].callback(wsi, reason, wsi->session_data, in, len);
}

void
fake_lws_trigger_protocol_callback(struct lws_context *context, enum lws_callback_reasons reason, void *in, size_t len)
{
    const struct lws_protocols *protocols;

    if (!context)
        return;

    protocols = context->protocols;
    if (!protocols || !protocols[0].callback)
        return;

    protocols[0].callback(NULL, reason, NULL, in, len);
}

struct lws_context *
lws_create_context(struct lws_context_creation_info *info)
{
    struct lws_context *context;

    if (state.create_context_fail)
        return NULL;

    context = calloc(1, sizeof(*context));
    if (!context)
        return NULL;

    context->protocols = info ? info->protocols : NULL;
    context->user = info ? info->user : NULL;
    context->options = info ? info->options : 0;
    context->port = info ? info->port : 0;
    context->next = state.contexts;
    state.contexts = context;
    state.last_context = context;

    state.last_context_info.port = context->port;
    state.last_context_info.options = context->options;
    state.last_context_info.user = context->user;
    state.last_context_info.protocols = context->protocols;

    return context;
}

void
lws_context_destroy(struct lws_context *context)
{
    struct lws_context **cursor;

    if (!context)
        return;

    cursor = &state.contexts;
    while (*cursor) {
        if (*cursor == context) {
            *cursor = context->next;
            break;
        }

        cursor = &(*cursor)->next;
    }

    if (state.last_context == context)
        state.last_context = NULL;

    if (state.last_wsi && state.last_wsi->context == context)
        state.last_wsi = NULL;

    state.destroy_call_count++;
    fake_lws_free_context(context);
}

void *
lws_context_user(struct lws_context *context)
{
    if (!context)
        return NULL;

    return context->user;
}

struct lws_context *
lws_get_context(struct lws *wsi)
{
    if (!wsi)
        return NULL;

    return wsi->context;
}

struct lws *
lws_client_connect_via_info(const struct lws_client_connect_info *info)
{
    memset(&state.last_connect_info, 0, sizeof(state.last_connect_info));

    if (info) {
        state.last_connect_info.context = info->context;
        state.last_connect_info.port = info->port;
        state.last_connect_info.ssl_connection = info->ssl_connection;
        fake_lws_copy_string(state.last_connect_info.address, sizeof(state.last_connect_info.address), info->address);
        fake_lws_copy_string(state.last_connect_info.path, sizeof(state.last_connect_info.path), info->path);
        fake_lws_copy_string(state.last_connect_info.host, sizeof(state.last_connect_info.host), info->host);
        fake_lws_copy_string(state.last_connect_info.origin, sizeof(state.last_connect_info.origin), info->origin);
        fake_lws_copy_string(state.last_connect_info.protocol, sizeof(state.last_connect_info.protocol), info->protocol);
    }

    if (state.client_connect_fail || !info)
        return NULL;

    return fake_lws_create_wsi(info->context);
}

size_t
lws_remaining_packet_payload(struct lws *wsi)
{
    (void) wsi;
    return state.remaining_packet_payload;
}

int
lws_is_final_fragment(struct lws *wsi)
{
    (void) wsi;
    return state.final_fragment;
}

int
lws_service(struct lws_context *context, int timeout_ms)
{
    state.service_call_count++;

    if (state.auto_callback_enabled) {
        struct lws *target = state.last_wsi;

        if (!target || target->context != context)
            target = fake_lws_create_wsi(context);

        state.auto_callback_enabled = 0;

        if (target)
            fake_lws_trigger_callback(target, state.auto_callback_reason,
                                      state.auto_callback_detail[0] ? state.auto_callback_detail : NULL,
                                      state.auto_callback_detail[0] ? strlen(state.auto_callback_detail) : 0);
    }

    (void) timeout_ms;
    return 0;
}

void
lws_cancel_service(struct lws_context *context)
{
    (void) context;
    state.cancel_service_call_count++;
}

void
lws_callback_on_writable(struct lws *wsi)
{
    (void) wsi;
    state.callback_on_writable_call_count++;
}

int
lws_write(struct lws *wsi, unsigned char *buf, size_t len, enum lws_write_protocol protocol)
{
    int result;

    (void) wsi;
    (void) protocol;

    state.write_call_count++;

    free(state.last_write_bytes);
    state.last_write_bytes = NULL;
    state.last_write_length = len;

    if (len > 0) {
        state.last_write_bytes = malloc(len);
        memcpy(state.last_write_bytes, buf, len);
    }

    if (state.queued_write_result_count > 0) {
        size_t index;

        result = state.queued_write_results[0];
        for (index = 1; index < state.queued_write_result_count; index++)
            state.queued_write_results[index - 1] = state.queued_write_results[index];
        state.queued_write_result_count--;
        return result;
    }

    return (int) len;
}
