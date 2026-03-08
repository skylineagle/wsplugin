#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstpushsrc.h>
#include <string.h>

#include "fake_lws_test_api.h"

static GstCaps *test_peer_query_caps_result = NULL;
static GstCaps *test_last_set_caps = NULL;
static gboolean test_base_src_set_caps_result = TRUE;

static GstCaps *
fake_gst_pad_peer_query_caps(GstPad *pad, GstCaps *filter)
{
    if (test_peer_query_caps_result)
        return gst_caps_ref(test_peer_query_caps_result);

    return gst_pad_peer_query_caps(pad, filter);
}

static gboolean
fake_gst_base_src_set_caps(GstBaseSrc *src, GstCaps *caps)
{
    if (test_peer_query_caps_result) {
        if (test_last_set_caps)
            gst_caps_unref(test_last_set_caps);

        test_last_set_caps = gst_caps_ref(caps);
        return test_base_src_set_caps_result;
    }

    return gst_base_src_set_caps(src, caps);
}

gboolean
gst_element_register_wssink(GstPlugin *plugin)
{
    (void) plugin;
    return TRUE;
}

gboolean
gst_element_register_datedmultifilesink(GstPlugin *plugin)
{
    (void) plugin;
    return TRUE;
}

#define gst_pad_peer_query_caps fake_gst_pad_peer_query_caps
#define gst_base_src_set_caps fake_gst_base_src_set_caps
#include "../src/gstwsplugin.c"
#include "../src/gstwssrc.c"
#undef gst_pad_peer_query_caps
#undef gst_base_src_set_caps

typedef struct {
    GstWsSrc *src;
    guint64 sleep_usecs;
} FlushThreadData;

static void
ensure_gstreamer(void)
{
    static gsize initialized = 0;

    if (g_once_init_enter(&initialized)) {
        gst_init(NULL, NULL);
        g_once_init_leave(&initialized, 1);
    }
}

static GstWsSrc *
create_src(void)
{
    ensure_gstreamer();
    fake_lws_reset();
    if (test_peer_query_caps_result) {
        gst_caps_unref(test_peer_query_caps_result);
        test_peer_query_caps_result = NULL;
    }
    if (test_last_set_caps) {
        gst_caps_unref(test_last_set_caps);
        test_last_set_caps = NULL;
    }
    test_base_src_set_caps_result = TRUE;
    return GST_WS_SRC(g_object_new(GST_TYPE_WS_SRC, NULL));
}

static void
destroy_src(GstWsSrc *src)
{
    gst_object_unref(src);
    if (test_peer_query_caps_result) {
        gst_caps_unref(test_peer_query_caps_result);
        test_peer_query_caps_result = NULL;
    }
    if (test_last_set_caps) {
        gst_caps_unref(test_last_set_caps);
        test_last_set_caps = NULL;
    }
    fake_lws_reset();
}

static struct lws_context *
create_src_context(GstWsSrc *src)
{
    static const struct lws_protocols protocols[] = {
        { "wsplugin", lws_callback_src, sizeof(WsSrcSession), 0, 0, NULL, 0 },
        LWS_PROTOCOL_LIST_TERM
    };
    struct lws_context_creation_info info;

    memset(&info, 0, sizeof(info));
    info.protocols = protocols;
    info.user = src;

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

static gchar *
buffer_to_string(GstBuffer *buffer)
{
    GstMapInfo map;
    gchar *text;

    g_assert_true(gst_buffer_map(buffer, &map, GST_MAP_READ));
    text = g_strndup((const gchar *) map.data, map.size);
    gst_buffer_unmap(buffer, &map);

    return text;
}

static gpointer
set_flushing_after_delay(gpointer data)
{
    FlushThreadData *thread_data = data;

    g_usleep(thread_data->sleep_usecs);
    thread_data->src->flushing = TRUE;
    g_async_queue_push(thread_data->src->queue, gst_buffer_new());

    return NULL;
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
test_mode_type_and_plugin_init(void)
{
    GEnumClass *enum_class;
    const GstPluginDesc *plugin_desc;

    ensure_gstreamer();

    g_assert_cmpuint(gst_ws_mode_get_type(), !=, 0);
    g_assert_true(plugin_init(NULL));
    plugin_desc = gst_plugin_wsplugin_get_desc();
    g_assert_nonnull(plugin_desc);
    gst_plugin_wsplugin_register();

    enum_class = g_type_class_ref(GST_TYPE_WS_MODE);
    g_assert_cmpuint(enum_class->n_values, ==, 2);
    g_type_class_unref(enum_class);
}

static void
test_defaults_and_properties(void)
{
    GstWsSrc *src = create_src();
    gchar *uri = NULL;
    gint port = 0;
    gint connect_timeout = 0;
    GstWsMode mode = GST_WS_MODE_SERVER;

    g_object_get(src,
                 "mode", &mode,
                 "uri", &uri,
                 "port", &port,
                 "connect-timeout", &connect_timeout,
                 NULL);

    g_assert_cmpint(mode, ==, GST_WS_DEFAULT_MODE);
    g_assert_cmpstr(uri, ==, GST_WS_DEFAULT_URI);
    g_assert_cmpint(port, ==, GST_WS_DEFAULT_PORT);
    g_assert_cmpint(connect_timeout, ==, GST_WS_DEFAULT_CONNECT_TIMEOUT);

    g_object_set(src,
                 "mode", GST_WS_MODE_SERVER,
                 "uri", "ws://example.test:9999/source",
                 "port", 9999,
                 "connect-timeout", 7,
                 NULL);

    g_object_get(src,
                 "mode", &mode,
                 "uri", &uri,
                 "port", &port,
                 "connect-timeout", &connect_timeout,
                 NULL);

    g_assert_cmpint(mode, ==, GST_WS_MODE_SERVER);
    g_assert_cmpstr(uri, ==, "ws://example.test:9999/source");
    g_assert_cmpint(port, ==, 9999);
    g_assert_cmpint(connect_timeout, ==, 7);

    g_free(uri);
    destroy_src(src);
}

static void
test_invalid_property_accessors(void)
{
    GstWsSrc *src = create_src();
    GValue value = G_VALUE_INIT;
    GParamSpec *pspec = g_param_spec_int("invalid", "invalid", "invalid", 0, 1, 0, G_PARAM_READWRITE);

    g_value_init(&value, G_TYPE_INT);
    g_value_set_int(&value, 1);

    g_test_log_set_fatal_handler(allow_invalid_property_warning, NULL);
    gst_ws_src_set_property(G_OBJECT(src), 999, &value, pspec);
    gst_ws_src_get_property(G_OBJECT(src), 999, &value, pspec);
    g_test_log_set_fatal_handler(NULL, NULL);

    g_value_unset(&value);
    g_param_spec_unref(pspec);
    destroy_src(src);
}

static void
test_callback_server_filters_and_lifecycle(void)
{
    GstWsSrc *src = create_src();
    struct lws *wsi;

    src->mode = GST_WS_MODE_SERVER;
    src->lws_ctx = create_src_context(src);
    wsi = fake_lws_create_wsi(src->lws_ctx);

    g_assert_cmpint(lws_callback_src(wsi, LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION, NULL, NULL, 0), ==, 0);
    g_assert_cmpint(lws_callback_src(wsi, LWS_CALLBACK_FILTER_NETWORK_CONNECTION, NULL, NULL, 0), ==, 0);
    g_assert_cmpint(lws_callback_src(wsi, (enum lws_callback_reasons) 999, fake_lws_get_session_data(wsi), NULL, 0), ==, 0);

    src->server_has_client = TRUE;
    g_assert_cmpint(lws_callback_src(wsi, LWS_CALLBACK_FILTER_NETWORK_CONNECTION, NULL, NULL, 0), ==, -1);

    src->server_has_client = FALSE;
    fake_lws_trigger_callback(wsi, LWS_CALLBACK_ESTABLISHED, NULL, 0);

    g_assert_true(src->connected);
    g_assert_true(src->server_has_client);
    g_assert_true(src->wsi == wsi);

    fake_lws_trigger_callback(wsi, LWS_CALLBACK_CLOSED, NULL, 0);

    g_assert_false(src->connected);
    g_assert_false(src->server_has_client);
    g_assert_null(src->wsi);

    destroy_src(src);
}

static void
test_callback_client_error_and_client_closed(void)
{
    GstWsSrc *src = create_src();
    GstBuffer *buffer = NULL;
    struct lws *wsi;

    src->mode = GST_WS_MODE_CLIENT;
    src->lws_ctx = create_src_context(src);
    wsi = fake_lws_create_wsi(src->lws_ctx);

    fake_lws_trigger_callback(wsi, LWS_CALLBACK_CLIENT_ESTABLISHED, NULL, 0);
    g_assert_true(src->connected);

    fake_lws_trigger_callback(wsi, LWS_CALLBACK_CLIENT_CONNECTION_ERROR, "boom", 4);
    g_assert_false(src->connected);
    g_assert_null(src->wsi);
    g_assert_cmpint(gst_ws_src_create(GST_PUSH_SRC(src), &buffer), ==, GST_FLOW_EOS);

    src->connected = TRUE;
    src->wsi = wsi;
    fake_lws_set_remaining_packet_payload(3);
    fake_lws_set_final_fragment(TRUE);
    fake_lws_trigger_callback(wsi, LWS_CALLBACK_CLIENT_RECEIVE, "abc", 3);
    fake_lws_trigger_callback(wsi, LWS_CALLBACK_CLIENT_CLOSED, NULL, 0);

    g_assert_false(src->connected);
    g_assert_cmpint(gst_ws_src_create(GST_PUSH_SRC(src), &buffer), ==, GST_FLOW_EOS);

    destroy_src(src);
}

static void
test_receive_and_create_paths(void)
{
    GstWsSrc *src = create_src();
    GstBuffer *buffer = NULL;
    gchar *text;
    struct lws *wsi;
    FlushThreadData thread_data;
    GThread *thread;

    src->mode = GST_WS_MODE_CLIENT;
    src->lws_ctx = create_src_context(src);
    wsi = fake_lws_create_wsi(src->lws_ctx);

    fake_lws_trigger_callback(wsi, LWS_CALLBACK_CLIENT_ESTABLISHED, NULL, 0);

    fake_lws_set_remaining_packet_payload(2);
    fake_lws_set_final_fragment(TRUE);
    fake_lws_trigger_callback(wsi, LWS_CALLBACK_RECEIVE, "hel", 3);
    fake_lws_set_remaining_packet_payload(0);
    fake_lws_trigger_callback(wsi, LWS_CALLBACK_RECEIVE, "lo", 2);

    g_assert_cmpint(gst_ws_src_create(GST_PUSH_SRC(src), &buffer), ==, GST_FLOW_OK);
    text = buffer_to_string(buffer);
    g_assert_cmpstr(text, ==, "hello");
    g_free(text);
    gst_buffer_unref(buffer);

    g_async_queue_push(src->queue, gst_buffer_new());
    g_async_queue_push(src->queue, create_buffer_from_text("next"));
    g_assert_cmpint(gst_ws_src_create(GST_PUSH_SRC(src), &buffer), ==, GST_FLOW_OK);
    text = buffer_to_string(buffer);
    g_assert_cmpstr(text, ==, "next");
    g_free(text);
    gst_buffer_unref(buffer);

    src->flushing = TRUE;
    g_assert_cmpint(gst_ws_src_create(GST_PUSH_SRC(src), &buffer), ==, GST_FLOW_FLUSHING);

    src->flushing = FALSE;
    thread_data.src = src;
    thread_data.sleep_usecs = 250000;
    thread = g_thread_new("wssrc-flush", set_flushing_after_delay, &thread_data);
    g_assert_cmpint(gst_ws_src_create(GST_PUSH_SRC(src), &buffer), ==, GST_FLOW_FLUSHING);
    g_thread_join(thread);

    destroy_src(src);
}

static void
test_negotiate_paths(void)
{
    GstWsSrc *src = create_src();
    g_assert_true(gst_ws_src_negotiate(GST_BASE_SRC(src)));

    test_peer_query_caps_result = gst_caps_new_any();
    g_assert_true(gst_ws_src_negotiate(GST_BASE_SRC(src)));
    gst_caps_unref(test_peer_query_caps_result);
    test_peer_query_caps_result = NULL;

    test_peer_query_caps_result = gst_caps_from_string("image/jpeg,width=(int)[1,4096]");
    g_assert_true(gst_ws_src_negotiate(GST_BASE_SRC(src)));
    g_assert_nonnull(test_last_set_caps);
    g_assert_true(gst_caps_is_fixed(test_last_set_caps));
    g_assert_cmpstr(gst_structure_get_name(gst_caps_get_structure(test_last_set_caps, 0)), ==, "image/jpeg");

    destroy_src(src);
}

static void
test_start_server_success_unlock_and_stop(void)
{
    GstWsSrc *src = create_src();
    FakeLwsContextInfoSnapshot context_info;

    src->mode = GST_WS_MODE_SERVER;
    src->port = 4321;
    fake_lws_set_auto_callback(LWS_CALLBACK_ESTABLISHED, TRUE);

    g_assert_true(gst_ws_src_start(GST_BASE_SRC(src)));
    context_info = fake_lws_get_last_context_info();

    g_assert_cmpint(context_info.port, ==, 4321);
    g_assert_cmpint(context_info.options, ==, LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT);
    g_assert_true(context_info.user == src);
    g_assert_true(fake_lws_get_service_call_count() > 0);

    g_async_queue_push(src->queue, create_buffer_from_text("queued"));
    g_async_queue_push(src->queue, gst_buffer_new());

    g_assert_true(gst_ws_src_unlock(GST_BASE_SRC(src)));
    g_assert_true(src->flushing);
    g_assert_true(gst_ws_src_unlock_stop(GST_BASE_SRC(src)));
    g_assert_false(src->flushing);
    g_assert_true(gst_ws_src_stop(GST_BASE_SRC(src)));
    g_assert_false(src->connected);
    g_assert_cmpint(fake_lws_get_cancel_service_call_count(), >, 0);

    destroy_src(src);
}

static void
test_start_client_success_with_secure_uri(void)
{
    GstWsSrc *src = create_src();
    FakeLwsConnectInfoSnapshot connect_info;

    src->mode = GST_WS_MODE_CLIENT;
    src->connect_timeout = 1;
    g_object_set(src, "uri", "wss://secure.example.test:9443/live", NULL);
    fake_lws_set_auto_callback(LWS_CALLBACK_CLIENT_ESTABLISHED, TRUE);

    g_assert_true(gst_ws_src_start(GST_BASE_SRC(src)));
    connect_info = fake_lws_get_last_connect_info();

    g_assert_cmpstr(connect_info.address, ==, "secure.example.test");
    g_assert_cmpstr(connect_info.host, ==, "secure.example.test");
    g_assert_cmpstr(connect_info.origin, ==, "secure.example.test");
    g_assert_cmpstr(connect_info.path, ==, "/live");
    g_assert_cmpstr(connect_info.protocol, ==, "wsplugin");
    g_assert_cmpint(connect_info.port, ==, 9443);
    g_assert_cmpint(connect_info.ssl_connection, ==, LCCSCF_USE_SSL);

    g_assert_true(gst_ws_src_stop(GST_BASE_SRC(src)));
    destroy_src(src);
}

static void
test_start_client_success_without_explicit_port(void)
{
    GstWsSrc *src = create_src();
    FakeLwsConnectInfoSnapshot connect_info;

    src->mode = GST_WS_MODE_CLIENT;
    src->connect_timeout = 1;
    g_object_set(src, "uri", "ws://plain.example.test/feed", NULL);
    fake_lws_set_auto_callback(LWS_CALLBACK_CLIENT_ESTABLISHED, TRUE);

    g_assert_true(gst_ws_src_start(GST_BASE_SRC(src)));
    connect_info = fake_lws_get_last_connect_info();

    g_assert_cmpstr(connect_info.address, ==, "plain.example.test");
    g_assert_cmpstr(connect_info.path, ==, "/feed");
    g_assert_cmpint(connect_info.port, ==, 80);
    g_assert_cmpint(connect_info.ssl_connection, ==, 0);

    g_assert_true(gst_ws_src_stop(GST_BASE_SRC(src)));
    destroy_src(src);
}

static void
test_start_failures(void)
{
    GstWsSrc *src = create_src();
    FakeLwsConnectInfoSnapshot connect_info;

    src->mode = GST_WS_MODE_CLIENT;
    src->connect_timeout = 1;
    g_object_set(src, "uri", "plain.example.test", NULL);
    fake_lws_set_client_connect_fail(TRUE);

    g_assert_false(gst_ws_src_start(GST_BASE_SRC(src)));
    connect_info = fake_lws_get_last_connect_info();
    g_assert_cmpstr(connect_info.address, ==, "plain.example.test");
    g_assert_cmpstr(connect_info.path, ==, "/");
    g_assert_cmpint(connect_info.port, ==, 80);
    g_assert_cmpint(fake_lws_get_destroy_call_count(), ==, 1);

    destroy_src(src);

    src = create_src();
    src->mode = GST_WS_MODE_CLIENT;
    fake_lws_set_create_context_fail(TRUE);
    g_assert_false(gst_ws_src_start(GST_BASE_SRC(src)));
    destroy_src(src);

    src = create_src();
    src->mode = GST_WS_MODE_SERVER;
    src->connect_timeout = 0;
    g_assert_false(gst_ws_src_start(GST_BASE_SRC(src)));
    g_assert_cmpint(fake_lws_get_destroy_call_count(), ==, 1);
    destroy_src(src);

    src = create_src();
    src->mode = GST_WS_MODE_CLIENT;
    src->connect_timeout = 1;
    fake_lws_set_auto_callback(LWS_CALLBACK_CLIENT_CONNECTION_ERROR, TRUE);
    fake_lws_set_auto_callback_detail("failed");
    g_assert_false(gst_ws_src_start(GST_BASE_SRC(src)));
    g_assert_cmpint(fake_lws_get_destroy_call_count(), ==, 1);
    destroy_src(src);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/wssrc/mode-type-and-plugin-init", test_mode_type_and_plugin_init);
    g_test_add_func("/wssrc/defaults-and-properties", test_defaults_and_properties);
    g_test_add_func("/wssrc/invalid-property-accessors", test_invalid_property_accessors);
    g_test_add_func("/wssrc/server-filters-and-lifecycle", test_callback_server_filters_and_lifecycle);
    g_test_add_func("/wssrc/client-error-and-client-closed", test_callback_client_error_and_client_closed);
    g_test_add_func("/wssrc/receive-and-create-paths", test_receive_and_create_paths);
    g_test_add_func("/wssrc/negotiate-paths", test_negotiate_paths);
    g_test_add_func("/wssrc/start-server-success-unlock-and-stop", test_start_server_success_unlock_and_stop);
    g_test_add_func("/wssrc/start-client-success-with-secure-uri", test_start_client_success_with_secure_uri);
    g_test_add_func("/wssrc/start-client-success-without-explicit-port", test_start_client_success_without_explicit_port);
    g_test_add_func("/wssrc/start-failures", test_start_failures);

    return g_test_run();
}
