#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstwssink.h"

GST_DEBUG_CATEGORY_STATIC (gst_ws_sink_debug);
#define GST_CAT_DEFAULT gst_ws_sink_debug

/* Properties */
enum {
    PROP_0,
    PROP_MODE,
    PROP_URI,
    PROP_PORT,
    PROP_CONNECT_TIMEOUT,
};

/* Pad template: accept any caps */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

#define gst_ws_sink_parent_class parent_class
G_DEFINE_TYPE (GstWsSink, gst_ws_sink, GST_TYPE_BASE_SINK);
GST_ELEMENT_REGISTER_DEFINE (wssink, "wssink", GST_RANK_NONE, GST_TYPE_WS_SINK);

/* ---- forward declarations ---- */
static void          gst_ws_sink_set_property (GObject *obj, guint id, const GValue *v, GParamSpec *ps);
static void          gst_ws_sink_get_property (GObject *obj, guint id, GValue *v, GParamSpec *ps);
static void          gst_ws_sink_finalize     (GObject *obj);
static gboolean      gst_ws_sink_start        (GstBaseSink *sink);
static gboolean      gst_ws_sink_stop         (GstBaseSink *sink);
static GstFlowReturn gst_ws_sink_render        (GstBaseSink *sink, GstBuffer *buf);

/* ---- LWS callback & thread ---- */
static int           lws_callback_sink        (struct lws *wsi, enum lws_callback_reasons reason,
                                               void *user, void *in, size_t len);
static gpointer      lws_thread_sink          (gpointer data);

/* ---- helpers ---- */
static WsSinkClient *ws_sink_client_new   (struct lws *wsi);
static void          ws_sink_client_free  (WsSinkClient *c);
static void          ws_sink_broadcast    (GstWsSink *self, GBytes *msg);

/* ---- class init ---- */
static void
gst_ws_sink_class_init (GstWsSinkClass *klass)
{
    GObjectClass    *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
    GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);

    GST_DEBUG_CATEGORY_INIT (gst_ws_sink_debug, "wssink", 0, "WebSocket sink");

    gobject_class->set_property = gst_ws_sink_set_property;
    gobject_class->get_property = gst_ws_sink_get_property;
    gobject_class->finalize     = gst_ws_sink_finalize;

    g_object_class_install_property (gobject_class, PROP_MODE,
        g_param_spec_enum ("mode", "Mode",
            "Connection mode: client connects to a server, server accepts multiple viewers",
            GST_TYPE_WS_MODE, GST_WS_DEFAULT_MODE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_URI,
        g_param_spec_string ("uri", "URI",
            "WebSocket URI to connect to (client mode)",
            GST_WS_DEFAULT_URI,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_PORT,
        g_param_spec_int ("port", "Port",
            "TCP port to listen on (server mode)",
            1, 65535, GST_WS_DEFAULT_PORT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_CONNECT_TIMEOUT,
        g_param_spec_int ("connect-timeout", "Connect Timeout",
            "Seconds to wait for initial connection before erroring (client mode)",
            1, 3600, GST_WS_DEFAULT_CONNECT_TIMEOUT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata (element_class,
        "WebSocket Sink",
        "Sink/Network",
        "Sends binary frames over a WebSocket connection",
        "wsplugin");

    gst_element_class_add_static_pad_template (element_class, &sink_template);

    basesink_class->start  = gst_ws_sink_start;
    basesink_class->stop   = gst_ws_sink_stop;
    basesink_class->render = gst_ws_sink_render;
}

/* ---- instance init ---- */
static void
gst_ws_sink_init (GstWsSink *self)
{
    self->mode            = GST_WS_DEFAULT_MODE;
    self->uri             = g_strdup (GST_WS_DEFAULT_URI);
    self->port            = GST_WS_DEFAULT_PORT;
    self->connect_timeout = GST_WS_DEFAULT_CONNECT_TIMEOUT;

    self->lws_ctx        = NULL;
    self->client_wsi     = NULL;
    self->clients        = NULL;
    self->connected      = FALSE;
    self->lws_thread     = NULL;
    self->thread_running = FALSE;
    self->flushing       = FALSE;

    g_mutex_init (&self->lock);
    g_cond_init  (&self->cond);

    /* Sink does not need to be async by default */
    gst_base_sink_set_sync (GST_BASE_SINK (self), FALSE);
}

/* ---- finalize ---- */
static void
gst_ws_sink_finalize (GObject *obj)
{
    GstWsSink *self = GST_WS_SINK (obj);
    g_free (self->uri);
    g_mutex_clear (&self->lock);
    g_cond_clear  (&self->cond);
    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* ---- properties ---- */
static void
gst_ws_sink_set_property (GObject *obj, guint id, const GValue *v, GParamSpec *ps)
{
    GstWsSink *self = GST_WS_SINK (obj);
    switch (id) {
        case PROP_MODE:    self->mode = g_value_get_enum (v); break;
        case PROP_URI:     g_free (self->uri); self->uri = g_value_dup_string (v); break;
        case PROP_PORT:    self->port = g_value_get_int (v); break;
        case PROP_CONNECT_TIMEOUT: self->connect_timeout = g_value_get_int (v); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, ps);
    }
}

static void
gst_ws_sink_get_property (GObject *obj, guint id, GValue *v, GParamSpec *ps)
{
    GstWsSink *self = GST_WS_SINK (obj);
    switch (id) {
        case PROP_MODE:    g_value_set_enum (v, self->mode); break;
        case PROP_URI:     g_value_set_string (v, self->uri); break;
        case PROP_PORT:    g_value_set_int (v, self->port); break;
        case PROP_CONNECT_TIMEOUT: g_value_set_int (v, self->connect_timeout); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, ps);
    }
}

/* ================================================================
 * Client list helpers
 * ================================================================ */

static WsSinkClient *
ws_sink_client_new (struct lws *wsi)
{
    WsSinkClient *c = g_new0 (WsSinkClient, 1);
    c->wsi          = wsi;
    /* Queue depth of 4 — if a client can't keep up we drop */
    c->send_queue   = g_async_queue_new_full ((GDestroyNotify) g_bytes_unref);
    c->current_msg  = NULL;
    c->current_offset = 0;
    return c;
}

static void
ws_sink_client_free (WsSinkClient *c)
{
    if (c->current_msg) {
        g_bytes_unref (c->current_msg);
        c->current_msg = NULL;
    }
    g_async_queue_unref (c->send_queue);
    g_free (c);
}

/* Find the WsSinkClient for a given wsi (must hold lock or be called from LWS thread only) */
static WsSinkClient *
ws_sink_find_client (GstWsSink *self, struct lws *wsi)
{
    for (GList *l = self->clients; l; l = l->next) {
        WsSinkClient *c = l->data;
        if (c->wsi == wsi)
            return c;
    }
    return NULL;
}

/* Queue a GBytes message for every connected client.
 * Non-blocking: if a client's queue is full the frame is dropped for that client.
 * NOTE: lws_callback_on_writable() must only be called from the LWS service
 * thread.  We only enqueue here and wake the LWS thread via lws_cancel_service();
 * the LWS thread calls lws_callback_on_writable() itself inside the
 * LWS_CALLBACK_EVENT_WAIT_CANCELLED callback. */
static void
ws_sink_broadcast (GstWsSink *self, GBytes *msg)
{
    gboolean any_queued = FALSE;

    g_mutex_lock (&self->lock);
    for (GList *l = self->clients; l; l = l->next) {
        WsSinkClient *c = l->data;
        /* Drop if the per-client send queue already has frames waiting.
         * Threshold of 16 gives ~half a second of buffer at 30fps while
         * still bounding memory and detecting genuinely slow clients. */
        if (g_async_queue_length (c->send_queue) >= 16) {
            GST_WARNING_OBJECT (self, "wssink: dropping frame for slow client %p", c->wsi);
            continue;
        }
        g_async_queue_push (c->send_queue, g_bytes_ref (msg));
        any_queued = TRUE;
    }
    g_mutex_unlock (&self->lock);

    /* Wake the LWS service loop; it will call lws_callback_on_writable()
     * for each client that has pending data (see LWS_CALLBACK_EVENT_WAIT_CANCELLED). */
    if (any_queued && self->lws_ctx)
        lws_cancel_service (self->lws_ctx);
}

/* ================================================================
 * LWS protocol callback
 * ================================================================ */

static int
lws_callback_sink (struct lws *wsi, enum lws_callback_reasons reason,
                   void *user, void *in, size_t len)
{
    /* wsi can be NULL for internal/protocol-level LWS callbacks; guard before
     * dereferencing it to obtain our user-data pointer. */
    if (!wsi)
        return 0;

    GstWsSink    *self = (GstWsSink *) lws_context_user (lws_get_context (wsi));
    WsSinkClient *client = NULL;

    switch (reason) {

    /* ---- Allow browser clients that send no Sec-WebSocket-Protocol header ---- */
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
        return 0; /* 0 = allow, regardless of requested subprotocol */

    /* ---- Server: new client connected ---- */
    case LWS_CALLBACK_ESTABLISHED:
        GST_INFO_OBJECT (self, "wssink server: client connected");
        {
            WsSinkClient *c = ws_sink_client_new (wsi);
            g_mutex_lock (&self->lock);
            self->clients  = g_list_prepend (self->clients, c);
            self->connected = TRUE;
            g_cond_broadcast (&self->cond);
            g_mutex_unlock (&self->lock);
        }
        break;

    /* ---- Client: handshake done ---- */
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        GST_INFO_OBJECT (self, "wssink client: connected to server");
        {
            WsSinkClient *c = ws_sink_client_new (wsi);
            g_mutex_lock (&self->lock);
            self->client_wsi = wsi;
            self->clients    = g_list_prepend (self->clients, c);
            self->connected  = TRUE;
            g_cond_broadcast (&self->cond);
            g_mutex_unlock (&self->lock);
        }
        break;

    /* ---- Woken by lws_cancel_service(): schedule writable callbacks ---- */
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
        g_mutex_lock (&self->lock);
        for (GList *l = self->clients; l; l = l->next) {
            WsSinkClient *c = l->data;
            if (g_async_queue_length (c->send_queue) > 0 || c->current_msg)
                lws_callback_on_writable (c->wsi);
        }
        g_mutex_unlock (&self->lock);
        break;

    /* ---- Writable: send next queued frame ---- */
    case LWS_CALLBACK_SERVER_WRITEABLE:
    case LWS_CALLBACK_CLIENT_WRITEABLE:
        /* clients list is only modified from the LWS thread (ESTABLISHED/CLOSED),
         * so no lock needed here — we are already on the LWS thread. */
        client = ws_sink_find_client (self, wsi);

        if (!client) break;

        /* Grab next message if we don't have one in flight */
        if (!client->current_msg) {
            client->current_msg = g_async_queue_try_pop (client->send_queue);
            client->current_offset = 0;
        }

        if (client->current_msg) {
            gsize    msg_size;
            const guint8 *msg_data = g_bytes_get_data (client->current_msg, &msg_size);
            gsize    remaining     = msg_size - client->current_offset;

            /* Allocate with LWS pre-padding */
            guint8 *send_buf = g_malloc (LWS_PRE + remaining);
            memcpy (send_buf + LWS_PRE, msg_data + client->current_offset, remaining);

            int written = lws_write (wsi, send_buf + LWS_PRE, remaining, LWS_WRITE_BINARY);
            g_free (send_buf);

            if (written < 0) {
                GST_WARNING_OBJECT (self, "wssink: lws_write failed for client %p (ret=%d)", wsi, written);
            } else {
                client->current_offset += (gsize) written;
                if (client->current_offset >= msg_size) {
                    /* Message fully sent */
                    g_bytes_unref (client->current_msg);
                    client->current_msg    = NULL;
                    client->current_offset = 0;
                }
            }

            /* If there are more messages or the current one isn't done, request another callback */
            if (client->current_msg || g_async_queue_length (client->send_queue) > 0)
                lws_callback_on_writable (wsi);
        }
        break;

    /* ---- Client connection error ---- */
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        GST_WARNING_OBJECT (self, "wssink: client connection error: %s",
                            in ? (char *)in : "(none)");
        /* fall through */

    /* ---- Connection closed ---- */
    case LWS_CALLBACK_CLOSED:
    case LWS_CALLBACK_CLIENT_CLOSED:
        GST_INFO_OBJECT (self, "wssink: connection closed (reason=%d)", reason);
        g_mutex_lock (&self->lock);
        client = ws_sink_find_client (self, wsi);
        if (client) {
            self->clients = g_list_remove (self->clients, client);
            ws_sink_client_free (client);
        }
        self->connected = (self->clients != NULL);
        g_cond_broadcast (&self->cond);
        g_mutex_unlock (&self->lock);
        break;

    default:
        break;
    }

    return 0;
}

/* ================================================================
 * LWS service thread
 * ================================================================ */

static gpointer
lws_thread_sink (gpointer data)
{
    GstWsSink *self = (GstWsSink *) data;
    GST_DEBUG_OBJECT (self, "LWS service thread started");
    while (self->thread_running) {
        lws_service (self->lws_ctx, 50);
    }
    GST_DEBUG_OBJECT (self, "LWS service thread exiting");
    return NULL;
}

/* ================================================================
 * GstBaseSink virtuals
 * ================================================================ */

static gboolean
gst_ws_sink_start (GstBaseSink *bsink)
{
    GstWsSink *self = GST_WS_SINK (bsink);

    GST_DEBUG_OBJECT (self, "start: mode=%d", self->mode);

    self->flushing  = FALSE;
    self->connected = FALSE;

    static const struct lws_protocols protocols[] = {
        { "wsplugin", lws_callback_sink, 0, 0, 0, NULL, 0 },
        /* "default" entry catches browsers that send no Sec-WebSocket-Protocol
         * header; LWS routes no-subprotocol WS upgrades through the entry
         * named "default", so we register the same callback here as a fallback. */
        { "default",  lws_callback_sink, 0, 0, 0, NULL, 0 },
        LWS_PROTOCOL_LIST_TERM
    };

    struct lws_context_creation_info ctx_info;
    memset (&ctx_info, 0, sizeof ctx_info);
    ctx_info.protocols = protocols;
    ctx_info.user      = self;
    ctx_info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    if (self->mode == GST_WS_MODE_SERVER) {
        ctx_info.port = self->port;
        GST_INFO_OBJECT (self, "wssink server: listening on port %d", self->port);
    } else {
        ctx_info.port = CONTEXT_PORT_NO_LISTEN;
    }

    self->lws_ctx = lws_create_context (&ctx_info);
    if (!self->lws_ctx) {
        GST_ERROR_OBJECT (self, "Failed to create lws context");
        return FALSE;
    }

    /* Client mode: connect outbound */
    if (self->mode == GST_WS_MODE_CLIENT) {
        const char *uri_str = self->uri;
        char host[256] = {0};
        char path[512] = "/";
        int  port_num  = 80;
        int  use_ssl   = 0;

        const char *rest = uri_str;
        if (g_str_has_prefix (uri_str, "wss://")) {
            use_ssl  = LCCSCF_USE_SSL;
            rest     = uri_str + 6;
            port_num = 443;
        } else if (g_str_has_prefix (uri_str, "ws://")) {
            rest = uri_str + 5;
        }

        const char *slash = strchr (rest, '/');
        const char *colon = strchr (rest, ':');
        if (colon && (!slash || colon < slash)) {
            gsize hlen = colon - rest;
            memcpy (host, rest, MIN (hlen, sizeof(host)-1));
            port_num = atoi (colon + 1);
        } else if (slash) {
            gsize hlen = slash - rest;
            memcpy (host, rest, MIN (hlen, sizeof(host)-1));
        } else {
            g_strlcpy (host, rest, sizeof(host));
        }
        if (slash)
            g_strlcpy (path, slash, sizeof(path));

        GST_INFO_OBJECT (self, "wssink client: connecting to %s:%d%s", host, port_num, path);

        struct lws_client_connect_info conn_info;
        memset (&conn_info, 0, sizeof conn_info);
        conn_info.context        = self->lws_ctx;
        conn_info.address        = host;
        conn_info.port           = port_num;
        conn_info.path           = path;
        conn_info.host           = host;
        conn_info.origin         = host;
        conn_info.protocol       = "wsplugin";
        conn_info.ssl_connection = use_ssl;

        self->client_wsi = lws_client_connect_via_info (&conn_info);
        if (!self->client_wsi) {
            GST_ERROR_OBJECT (self, "lws_client_connect_via_info failed");
            lws_context_destroy (self->lws_ctx);
            self->lws_ctx = NULL;
            return FALSE;
        }
    }

    /* Start LWS service thread */
    self->thread_running = TRUE;
    self->lws_thread = g_thread_new ("wssink-lws", lws_thread_sink, self);

    /* In client mode wait for connection; server mode returns immediately
     * (it starts accepting after the thread is up) */
    if (self->mode == GST_WS_MODE_CLIENT) {
        gint64 deadline = g_get_monotonic_time () +
                          (gint64) self->connect_timeout * G_TIME_SPAN_SECOND;
        g_mutex_lock (&self->lock);
        while (!self->connected && !self->flushing) {
            if (!g_cond_wait_until (&self->cond, &self->lock, deadline)) {
                GST_ERROR_OBJECT (self, "wssink: connection timed out after %ds",
                                  self->connect_timeout);
                g_mutex_unlock (&self->lock);
                self->thread_running = FALSE;
                lws_cancel_service (self->lws_ctx);
                g_thread_join (self->lws_thread);
                self->lws_thread = NULL;
                lws_context_destroy (self->lws_ctx);
                self->lws_ctx = NULL;
                return FALSE;
            }
        }
        g_mutex_unlock (&self->lock);
    }

    return TRUE;
}

static gboolean
gst_ws_sink_stop (GstBaseSink *bsink)
{
    GstWsSink *self = GST_WS_SINK (bsink);

    GST_DEBUG_OBJECT (self, "stop");
    self->flushing = TRUE;

    if (self->lws_thread) {
        self->thread_running = FALSE;
        lws_cancel_service (self->lws_ctx);
        g_thread_join (self->lws_thread);
        self->lws_thread = NULL;
    }

    if (self->lws_ctx) {
        lws_context_destroy (self->lws_ctx);
        self->lws_ctx = NULL;
    }

    g_mutex_lock (&self->lock);
    for (GList *l = self->clients; l; l = l->next)
        ws_sink_client_free (l->data);
    g_list_free (self->clients);
    self->clients    = NULL;
    self->connected  = FALSE;
    self->client_wsi = NULL;
    g_mutex_unlock (&self->lock);

    return TRUE;
}

static GstFlowReturn
gst_ws_sink_render (GstBaseSink *bsink, GstBuffer *buf)
{
    GstWsSink *self = GST_WS_SINK (bsink);

    if (self->flushing)
        return GST_FLOW_FLUSHING;

    /* In server mode it's fine to have no clients yet — just drop */
    g_mutex_lock (&self->lock);
    gboolean has_clients = (self->clients != NULL);
    g_mutex_unlock (&self->lock);

    if (!has_clients) {
        GST_LOG_OBJECT (self, "wssink: no clients connected, dropping buffer");
        return GST_FLOW_OK;
    }

    /* Map buffer and wrap it in GBytes for ref-counted sharing across clients */
    GstMapInfo map;
    if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT (self, "wssink: failed to map buffer");
        return GST_FLOW_ERROR;
    }

    GBytes *msg = g_bytes_new (map.data, map.size);
    gst_buffer_unmap (buf, &map);

    ws_sink_broadcast (self, msg);
    g_bytes_unref (msg);

    return GST_FLOW_OK;
}
