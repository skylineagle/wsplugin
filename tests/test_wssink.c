#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <string.h>

#include "fake_lws_test_api.h"

static gboolean test_force_buffer_map_failure = FALSE;

static gboolean
fake_gst_buffer_map(GstBuffer *buffer, GstMapInfo *info, GstMapFlags flags)
{
    if (test_force_buffer_map_failure)
        return FALSE;

    return gst_buffer_map(buffer, info, flags);
}

static void
fake_gst_buffer_unmap(GstBuffer *buffer, GstMapInfo *info)
{
    gst_buffer_unmap(buffer, info);
}

gboolean
gst_element_register_wssrc(GstPlugin *plugin)
{
    (void) plugin;
    return TRUE;
}

#define gst_buffer_map fake_gst_buffer_map
#define gst_buffer_unmap fake_gst_buffer_unmap
#include "../src/gstwsplugin.c"
#include "../src/gstwssink.c"
#undef gst_buffer_map
#undef gst_buffer_unmap

static void
ensure_gstreamer(void)
{
    static gsize initialized = 0;

    if (g_once_init_enter(&initialized)) {
        gst_init(NULL, NULL);
        g_once_init_leave(&initialized, 1);
    }
}

static GstWsSink *
create_sink(void)
{
    ensure_gstreamer();
    fake_lws_reset();
    test_force_buffer_map_failure = FALSE;
    return GST_WS_SINK(g_object_new(GST_TYPE_WS_SINK, NULL));
}

static void
destroy_sink(GstWsSink *sink)
{
    gst_object_unref(sink);
    fake_lws_reset();
    test_force_buffer_map_failure = FALSE;
}

static struct lws_context *
create_sink_context(GstWsSink *sink)
{
    static const struct lws_protocols protocols[] = {
        { "wsplugin", lws_callback_sink, 0, 0, 0, NULL, 0 },
        LWS_PROTOCOL_LIST_TERM
    };
    struct lws_context_creation_info info;

    memset(&info, 0, sizeof(info));
    info.protocols = protocols;
    info.user = sink;

    return lws_create_context(&info);
}

static GstBuffer *
create_buffer_from_text(const gchar *text)
{
    gsize len = strlen(text);
    GstBuffer *buffer = gst_buffer_new_allocate(NULL, len, NULL);
    GstMapInfo map;

    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    memcpy(map.data, text, len);
    gst_buffer_unmap(buffer, &map);

    return buffer;
}

static gboolean
allow_invalid_property_warning(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data)
{
    (void) log_domain;
    (void) log_level;
    (void) user_data;

    return g_strstr_len(message, -1, "invalid property id") == NULL;
}

static void
test_defaults_and_properties(void)
{
    GstWsSink *sink = create_sink();
    gchar *uri = NULL;
    gint port = 0;
    gint connect_timeout = 0;
    GstWsMode mode = GST_WS_MODE_SERVER;

    g_object_get(sink,
                 "mode", &mode,
                 "uri", &uri,
                 "port", &port,
                 "connect-timeout", &connect_timeout,
                 NULL);

    g_assert_cmpint(mode, ==, GST_WS_DEFAULT_MODE);
    g_assert_cmpstr(uri, ==, GST_WS_DEFAULT_URI);
    g_assert_cmpint(port, ==, GST_WS_DEFAULT_PORT);
    g_assert_cmpint(connect_timeout, ==, GST_WS_DEFAULT_CONNECT_TIMEOUT);
    g_free(uri);

    g_object_set(sink,
                 "mode", GST_WS_MODE_SERVER,
                 "uri", "ws://viewer.example.test:8888/feed",
                 "port", 8888,
                 "connect-timeout", 5,
                 NULL);

    g_object_get(sink,
                 "mode", &mode,
                 "uri", &uri,
                 "port", &port,
                 "connect-timeout", &connect_timeout,
                 NULL);

    g_assert_cmpint(mode, ==, GST_WS_MODE_SERVER);
    g_assert_cmpstr(uri, ==, "ws://viewer.example.test:8888/feed");
    g_assert_cmpint(port, ==, 8888);
    g_assert_cmpint(connect_timeout, ==, 5);
    g_assert_true(gst_element_register_wssink(NULL));

    g_free(uri);
    destroy_sink(sink);
}

static void
test_invalid_property_accessors(void)
{
    GstWsSink *sink = create_sink();
    GValue value = G_VALUE_INIT;
    GParamSpec *pspec = g_param_spec_int("invalid", "invalid", "invalid", 0, 1, 0, G_PARAM_READWRITE);

    g_value_init(&value, G_TYPE_INT);
    g_value_set_int(&value, 1);

    g_test_log_set_fatal_handler(allow_invalid_property_warning, NULL);
    gst_ws_sink_set_property(G_OBJECT(sink), 999, &value, pspec);
    gst_ws_sink_get_property(G_OBJECT(sink), 999, &value, pspec);
    g_test_log_set_fatal_handler(NULL, NULL);

    g_value_unset(&value);
    g_param_spec_unref(pspec);
    destroy_sink(sink);
}

static void
test_client_helpers_and_broadcast(void)
{
    GstWsSink *sink = create_sink();
    struct lws *wsi_a;
    struct lws *wsi_b;
    WsSinkClient *client_a;
    WsSinkClient *client_b;
    GBytes *payload;
    gint index;

    sink->lws_ctx = create_sink_context(sink);
    wsi_a = fake_lws_create_wsi(sink->lws_ctx);
    wsi_b = fake_lws_create_wsi(sink->lws_ctx);
    client_a = ws_sink_client_new(wsi_a);
    client_b = ws_sink_client_new(wsi_b);

    sink->clients = g_list_prepend(sink->clients, client_a);
    sink->clients = g_list_prepend(sink->clients, client_b);
    sink->connected = TRUE;

    g_assert_true(ws_sink_find_client(sink, wsi_a) == client_a);
    g_assert_true(ws_sink_find_client(sink, wsi_b) == client_b);

    for (index = 0; index < 16; index++)
        g_async_queue_push(client_a->send_queue, g_bytes_new_static("x", 1));

    payload = g_bytes_new_static("payload", 7);
    ws_sink_broadcast(sink, payload);

    g_assert_cmpint(g_async_queue_length(client_a->send_queue), ==, 16);
    g_assert_cmpint(g_async_queue_length(client_b->send_queue), ==, 1);
    g_assert_cmpint(fake_lws_get_cancel_service_call_count(), ==, 1);

    g_bytes_unref(payload);
    destroy_sink(sink);
}

static void
test_callback_paths(void)
{
    GstWsSink *sink = create_sink();
    struct lws *server_wsi;
    struct lws *client_wsi;
    struct lws *other_wsi;
    WsSinkClient *first_client;
    WsSinkClient *second_client;

    sink->lws_ctx = create_sink_context(sink);
    server_wsi = fake_lws_create_wsi(sink->lws_ctx);
    client_wsi = fake_lws_create_wsi(sink->lws_ctx);
    other_wsi = fake_lws_create_wsi(sink->lws_ctx);

    g_assert_cmpint(lws_callback_sink(NULL, LWS_CALLBACK_CLOSED, NULL, NULL, 0), ==, 0);
    g_assert_cmpint(lws_callback_sink(server_wsi, LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION, NULL, NULL, 0), ==, 0);

    fake_lws_trigger_callback(server_wsi, LWS_CALLBACK_ESTABLISHED, NULL, 0);
    fake_lws_trigger_callback(client_wsi, LWS_CALLBACK_CLIENT_ESTABLISHED, NULL, 0);

    g_assert_nonnull(sink->clients);
    g_assert_true(sink->connected);
    g_assert_true(sink->client_wsi == client_wsi);

    first_client = sink->clients->data;
    second_client = sink->clients->next->data;
    g_async_queue_push(first_client->send_queue, g_bytes_new_static("one", 3));
    second_client->current_msg = g_bytes_new_static("two", 3);

    fake_lws_trigger_callback(server_wsi, LWS_CALLBACK_EVENT_WAIT_CANCELLED, NULL, 0);
    g_assert_cmpint(fake_lws_get_callback_on_writable_call_count(), ==, 2);

    fake_lws_trigger_callback(other_wsi, LWS_CALLBACK_SERVER_WRITEABLE, NULL, 0);

    fake_lws_queue_write_result(2);
    fake_lws_trigger_callback(server_wsi, LWS_CALLBACK_SERVER_WRITEABLE, NULL, 0);
    g_assert_nonnull(second_client->current_msg);
    g_assert_cmpuint(second_client->current_offset, ==, 2);

    fake_lws_queue_write_result(1);
    fake_lws_trigger_callback(server_wsi, LWS_CALLBACK_SERVER_WRITEABLE, NULL, 0);
    g_assert_null(second_client->current_msg);
    g_assert_cmpuint(second_client->current_offset, ==, 0);

    fake_lws_trigger_callback(client_wsi, LWS_CALLBACK_CLIENT_WRITEABLE, NULL, 0);
    g_assert_cmpint(fake_lws_get_write_call_count(), ==, 3);

    second_client->current_msg = g_bytes_new_static("err", 3);
    second_client->current_offset = 0;
    fake_lws_queue_write_result(-1);
    fake_lws_trigger_callback(server_wsi, LWS_CALLBACK_SERVER_WRITEABLE, NULL, 0);
    g_assert_nonnull(second_client->current_msg);

    fake_lws_trigger_callback(client_wsi, (enum lws_callback_reasons) 999, NULL, 0);
    fake_lws_trigger_callback(client_wsi, LWS_CALLBACK_CLIENT_CONNECTION_ERROR, "nope", 4);
    g_assert_true(sink->connected);
    g_assert_cmpuint(g_list_length(sink->clients), ==, 1);

    fake_lws_trigger_callback(server_wsi, LWS_CALLBACK_CLOSED, NULL, 0);
    fake_lws_trigger_callback(other_wsi, LWS_CALLBACK_CLIENT_CLOSED, NULL, 0);
    g_assert_false(sink->connected);
    g_assert_null(sink->clients);

    destroy_sink(sink);
}

static void
test_start_server_and_stop(void)
{
    GstWsSink *sink = create_sink();
    FakeLwsContextInfoSnapshot context_info;

    sink->mode = GST_WS_MODE_SERVER;
    sink->port = 5001;

    g_assert_true(gst_ws_sink_start(GST_BASE_SINK(sink)));
    context_info = fake_lws_get_last_context_info();

    g_assert_cmpint(context_info.port, ==, 5001);
    g_assert_cmpint(context_info.options, ==, LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT);
    g_assert_true(context_info.user == sink);

    fake_lws_trigger_callback(fake_lws_create_wsi(sink->lws_ctx), LWS_CALLBACK_ESTABLISHED, NULL, 0);
    fake_lws_trigger_callback(fake_lws_create_wsi(sink->lws_ctx), LWS_CALLBACK_ESTABLISHED, NULL, 0);
    g_assert_true(sink->connected);

    g_assert_true(gst_ws_sink_stop(GST_BASE_SINK(sink)));
    g_assert_false(sink->connected);
    g_assert_null(sink->clients);
    g_assert_cmpint(fake_lws_get_cancel_service_call_count(), >, 0);

    destroy_sink(sink);
}

static void
test_start_client_success_with_secure_uri(void)
{
    GstWsSink *sink = create_sink();
    FakeLwsConnectInfoSnapshot connect_info;

    sink->mode = GST_WS_MODE_CLIENT;
    sink->connect_timeout = 1;
    g_object_set(sink, "uri", "wss://sink.example.test:9443/live", NULL);
    fake_lws_set_auto_callback(LWS_CALLBACK_CLIENT_ESTABLISHED, TRUE);

    g_assert_true(gst_ws_sink_start(GST_BASE_SINK(sink)));
    connect_info = fake_lws_get_last_connect_info();

    g_assert_cmpstr(connect_info.address, ==, "sink.example.test");
    g_assert_cmpstr(connect_info.path, ==, "/live");
    g_assert_cmpint(connect_info.port, ==, 9443);
    g_assert_cmpint(connect_info.ssl_connection, ==, LCCSCF_USE_SSL);

    g_assert_true(gst_ws_sink_stop(GST_BASE_SINK(sink)));
    destroy_sink(sink);
}

static void
test_start_client_success_without_explicit_port(void)
{
    GstWsSink *sink = create_sink();
    FakeLwsConnectInfoSnapshot connect_info;

    sink->mode = GST_WS_MODE_CLIENT;
    sink->connect_timeout = 1;
    g_object_set(sink, "uri", "ws://sink.example.test/live", NULL);
    fake_lws_set_auto_callback(LWS_CALLBACK_CLIENT_ESTABLISHED, TRUE);

    g_assert_true(gst_ws_sink_start(GST_BASE_SINK(sink)));
    connect_info = fake_lws_get_last_connect_info();

    g_assert_cmpstr(connect_info.address, ==, "sink.example.test");
    g_assert_cmpstr(connect_info.path, ==, "/live");
    g_assert_cmpint(connect_info.port, ==, 80);
    g_assert_cmpint(connect_info.ssl_connection, ==, 0);

    g_assert_true(gst_ws_sink_stop(GST_BASE_SINK(sink)));
    destroy_sink(sink);
}

static void
test_start_failures(void)
{
    GstWsSink *sink = create_sink();
    FakeLwsConnectInfoSnapshot connect_info;

    sink->mode = GST_WS_MODE_CLIENT;
    sink->connect_timeout = 1;
    g_object_set(sink, "uri", "plain-sink.example.test", NULL);
    fake_lws_set_client_connect_fail(TRUE);

    g_assert_false(gst_ws_sink_start(GST_BASE_SINK(sink)));
    connect_info = fake_lws_get_last_connect_info();
    g_assert_cmpstr(connect_info.address, ==, "plain-sink.example.test");
    g_assert_cmpstr(connect_info.path, ==, "/");
    g_assert_cmpint(connect_info.port, ==, 80);
    g_assert_cmpint(fake_lws_get_destroy_call_count(), ==, 1);

    destroy_sink(sink);

    sink = create_sink();
    sink->mode = GST_WS_MODE_CLIENT;
    fake_lws_set_create_context_fail(TRUE);
    g_assert_false(gst_ws_sink_start(GST_BASE_SINK(sink)));
    destroy_sink(sink);

    sink = create_sink();
    sink->mode = GST_WS_MODE_CLIENT;
    sink->connect_timeout = 0;
    g_assert_false(gst_ws_sink_start(GST_BASE_SINK(sink)));
    g_assert_cmpint(fake_lws_get_destroy_call_count(), ==, 1);
    destroy_sink(sink);
}

static void
test_render_paths(void)
{
    GstWsSink *sink = create_sink();
    struct lws *wsi;
    GstBuffer *buffer;
    WsSinkClient *client;
    GBytes *queued;
    gsize size = 0;
    const guint8 *bytes;

    sink->flushing = TRUE;
    buffer = create_buffer_from_text("ignored");
    g_assert_cmpint(gst_ws_sink_render(GST_BASE_SINK(sink), buffer), ==, GST_FLOW_FLUSHING);
    gst_buffer_unref(buffer);

    sink->flushing = FALSE;
    buffer = create_buffer_from_text("no-clients");
    g_assert_cmpint(gst_ws_sink_render(GST_BASE_SINK(sink), buffer), ==, GST_FLOW_OK);
    gst_buffer_unref(buffer);

    sink->lws_ctx = create_sink_context(sink);
    wsi = fake_lws_create_wsi(sink->lws_ctx);
    client = ws_sink_client_new(wsi);
    sink->clients = g_list_prepend(sink->clients, client);

    test_force_buffer_map_failure = TRUE;
    buffer = create_buffer_from_text("map-fail");
    g_assert_cmpint(gst_ws_sink_render(GST_BASE_SINK(sink), buffer), ==, GST_FLOW_ERROR);
    gst_buffer_unref(buffer);
    test_force_buffer_map_failure = FALSE;

    buffer = create_buffer_from_text("frame");
    g_assert_cmpint(gst_ws_sink_render(GST_BASE_SINK(sink), buffer), ==, GST_FLOW_OK);
    gst_buffer_unref(buffer);

    g_assert_cmpint(g_async_queue_length(client->send_queue), ==, 1);
    g_assert_cmpint(fake_lws_get_cancel_service_call_count(), ==, 1);

    queued = g_async_queue_try_pop(client->send_queue);
    bytes = g_bytes_get_data(queued, &size);
    g_assert_cmpuint(size, ==, 5);
    g_assert_cmpmem(bytes, size, "frame", 5);
    g_bytes_unref(queued);

    destroy_sink(sink);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/wssink/defaults-and-properties", test_defaults_and_properties);
    g_test_add_func("/wssink/invalid-property-accessors", test_invalid_property_accessors);
    g_test_add_func("/wssink/client-helpers-and-broadcast", test_client_helpers_and_broadcast);
    g_test_add_func("/wssink/callback-paths", test_callback_paths);
    g_test_add_func("/wssink/start-server-and-stop", test_start_server_and_stop);
    g_test_add_func("/wssink/start-client-success-with-secure-uri", test_start_client_success_with_secure_uri);
    g_test_add_func("/wssink/start-client-success-without-explicit-port", test_start_client_success_without_explicit_port);
    g_test_add_func("/wssink/start-failures", test_start_failures);
    g_test_add_func("/wssink/render-paths", test_render_paths);

    return g_test_run();
}
