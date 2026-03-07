#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwssrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_ws_src_debug);
#define GST_CAT_DEFAULT gst_ws_src_debug

/* Properties */
enum {
    PROP_0,
    PROP_MODE,
    PROP_URI,
    PROP_PORT,
    PROP_CONNECT_TIMEOUT,
};

/* Pad template: accept any caps — the pipeline decides what the bytes mean */
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

#define gst_ws_src_parent_class parent_class
G_DEFINE_TYPE (GstWsSrc, gst_ws_src, GST_TYPE_PUSH_SRC);
GST_ELEMENT_REGISTER_DEFINE (wssrc, "wssrc", GST_RANK_NONE, GST_TYPE_WS_SRC);

/* ---- forward declarations ---- */
static void     gst_ws_src_set_property (GObject *obj, guint id, const GValue *v, GParamSpec *ps);
static void     gst_ws_src_get_property (GObject *obj, guint id, GValue *v, GParamSpec *ps);
static void     gst_ws_src_finalize     (GObject *obj);
static gboolean gst_ws_src_start        (GstBaseSrc *src);
static gboolean gst_ws_src_stop         (GstBaseSrc *src);
static GstFlowReturn gst_ws_src_create  (GstPushSrc *src, GstBuffer **buf);
static gboolean gst_ws_src_unlock       (GstBaseSrc *src);
static gboolean gst_ws_src_unlock_stop  (GstBaseSrc *src);
static gboolean gst_ws_src_negotiate    (GstBaseSrc *src);

/* ---- LWS callback & thread ---- */
static int      lws_callback_src        (struct lws *wsi, enum lws_callback_reasons reason,
                                         void *user, void *in, size_t len);
static gpointer lws_thread_src          (gpointer data);

/* ---- class init ---- */
static void
gst_ws_src_class_init (GstWsSrcClass *klass)
{
    GObjectClass    *gobject_class  = G_OBJECT_CLASS (klass);
    GstElementClass *element_class  = GST_ELEMENT_CLASS (klass);
    GstBaseSrcClass *basesrc_class  = GST_BASE_SRC_CLASS (klass);
    GstPushSrcClass *pushsrc_class  = GST_PUSH_SRC_CLASS (klass);

    GST_DEBUG_CATEGORY_INIT (gst_ws_src_debug, "wssrc", 0, "WebSocket source");

    gobject_class->set_property = gst_ws_src_set_property;
    gobject_class->get_property = gst_ws_src_get_property;
    gobject_class->finalize     = gst_ws_src_finalize;

    g_object_class_install_property (gobject_class, PROP_MODE,
        g_param_spec_enum ("mode", "Mode",
            "Connection mode: client connects to a server, server listens for one client",
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
            "Seconds to wait for a connection before erroring",
            1, 3600, GST_WS_DEFAULT_CONNECT_TIMEOUT,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata (element_class,
        "WebSocket Source",
        "Source/Network",
        "Receives binary frames over a WebSocket connection",
        "wsplugin");

    gst_element_class_add_static_pad_template (element_class, &src_template);

    basesrc_class->negotiate   = gst_ws_src_negotiate;
    basesrc_class->start       = gst_ws_src_start;
    basesrc_class->stop        = gst_ws_src_stop;
    basesrc_class->unlock      = gst_ws_src_unlock;
    basesrc_class->unlock_stop = gst_ws_src_unlock_stop;

    pushsrc_class->create = gst_ws_src_create;
}

/* ---- instance init ---- */
static void
gst_ws_src_init (GstWsSrc *self)
{
    self->mode            = GST_WS_DEFAULT_MODE;
    self->uri             = g_strdup (GST_WS_DEFAULT_URI);
    self->port            = GST_WS_DEFAULT_PORT;
    self->connect_timeout = GST_WS_DEFAULT_CONNECT_TIMEOUT;

    self->lws_ctx         = NULL;
    self->wsi             = NULL;
    self->connected       = FALSE;
    self->server_has_client = FALSE;
    self->lws_thread      = NULL;
    self->thread_running  = FALSE;
    self->flushing        = FALSE;

    self->queue = g_async_queue_new_full ((GDestroyNotify) gst_buffer_unref);

    g_mutex_init (&self->lock);
    g_cond_init  (&self->cond);

    /* Live source: network data arrives asynchronously.
     * FORMAT_BYTES means GstBaseSrc won't timestamp-gate our buffers. */
    gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
    gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_BYTES);
}

/* ---- finalize ---- */
static void
gst_ws_src_finalize (GObject *obj)
{
    GstWsSrc *self = GST_WS_SRC (obj);

    g_free (self->uri);
    g_async_queue_unref (self->queue);
    g_mutex_clear (&self->lock);
    g_cond_clear  (&self->cond);

    G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* ---- properties ---- */
static void
gst_ws_src_set_property (GObject *obj, guint id, const GValue *v, GParamSpec *ps)
{
    GstWsSrc *self = GST_WS_SRC (obj);
    switch (id) {
        case PROP_MODE:
            self->mode = g_value_get_enum (v);
            break;
        case PROP_URI:
            g_free (self->uri);
            self->uri = g_value_dup_string (v);
            break;
        case PROP_PORT:
            self->port = g_value_get_int (v);
            break;
        case PROP_CONNECT_TIMEOUT:
            self->connect_timeout = g_value_get_int (v);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, ps);
    }
}

static void
gst_ws_src_get_property (GObject *obj, guint id, GValue *v, GParamSpec *ps)
{
    GstWsSrc *self = GST_WS_SRC (obj);
    switch (id) {
        case PROP_MODE:
            g_value_set_enum (v, self->mode);
            break;
        case PROP_URI:
            g_value_set_string (v, self->uri);
            break;
        case PROP_PORT:
            g_value_set_int (v, self->port);
            break;
        case PROP_CONNECT_TIMEOUT:
            g_value_set_int (v, self->connect_timeout);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, ps);
    }
}

/* ================================================================
 * LWS protocol callback
 * ================================================================ */

/* Per-session user data stored in the lws session */
typedef struct {
    GstWsSrc *src;
    /* Accumulation buffer for fragmented messages */
    guint8   *frag_buf;
    gsize     frag_len;
    gsize     frag_cap;
} WsSrcSession;

static int
lws_callback_src (struct lws *wsi, enum lws_callback_reasons reason,
                  void *user, void *in, size_t len)
{
    WsSrcSession *sess = (WsSrcSession *) user;
    /* Always fetch self from the context so we can log even before session init */
    GstWsSrc     *self = (GstWsSrc *) lws_context_user (lws_get_context (wsi));

    /* After CLIENT_ESTABLISHED sess->src is also set; prefer sess->src
     * only when it differs (it won't, but be safe). */
    if (sess && sess->src)
        self = sess->src;

    switch (reason) {

    /* ---- Allow browser clients that send no Sec-WebSocket-Protocol header ---- */
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
        return 0; /* 0 = allow, regardless of requested subprotocol */

    /* ---- SERVER: new incoming connection ---- */
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
        /* Called before user data is set up; we use the vhost user pointer */
        {
            GstWsSrc *s = (GstWsSrc *) lws_context_user (lws_get_context (wsi));
            if (s->server_has_client) {
                GST_INFO_OBJECT (s, "wssrc server: refusing extra client");
                return -1; /* reject */
            }
        }
        return 0;

    case LWS_CALLBACK_ESTABLISHED:
        sess->src      = (GstWsSrc *) lws_context_user (lws_get_context (wsi));
        sess->frag_buf = NULL;
        sess->frag_len = 0;
        sess->frag_cap = 0;
        self           = sess->src;
        g_mutex_lock (&self->lock);
        if (self->mode == GST_WS_MODE_SERVER) {
            self->server_has_client = TRUE;
        }
        self->wsi       = wsi;
        self->connected = TRUE;
        g_cond_broadcast (&self->cond);
        g_mutex_unlock (&self->lock);
        GST_INFO_OBJECT (self, "wssrc: connection established");
        break;

    /* ---- CLIENT: handshake done ---- */
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        sess->src      = (GstWsSrc *) lws_context_user (lws_get_context (wsi));
        sess->frag_buf = NULL;
        sess->frag_len = 0;
        sess->frag_cap = 0;
        self           = sess->src;
        g_mutex_lock (&self->lock);
        self->wsi       = wsi;
        self->connected = TRUE;
        g_cond_broadcast (&self->cond);
        g_mutex_unlock (&self->lock);
        GST_INFO_OBJECT (self, "wssrc: client connected to server");
        break;

    /* ---- Received data ---- */
    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
    {
        if (!self) break;

        /* lws_is_first_fragment / lws_is_final_fragment reflect the WebSocket
         * protocol FIN bit, not LWS's internal rx chunking.  For a large message
         * sent as a single unfragmented WS frame (FIN=1 always), both return TRUE
         * on every internal chunk — so we cannot use them to detect the end of the
         * payload.  Instead we use lws_remaining_packet_payload() which tells us
         * how many bytes of THIS WS message are still to come after this chunk. */
        gsize remaining_after = lws_remaining_packet_payload (wsi);
        gboolean is_last_chunk = (remaining_after == 0) && lws_is_final_fragment (wsi);

        /* Accumulate */
        gsize new_len = sess->frag_len + len;
        if (new_len > sess->frag_cap) {
            sess->frag_cap = MAX (new_len * 2, (gsize) 65536);
            sess->frag_buf = g_realloc (sess->frag_buf, sess->frag_cap);
        }
        memcpy (sess->frag_buf + sess->frag_len, in, len);
        sess->frag_len = new_len;

        if (is_last_chunk) {
            /* Complete message — wrap in a GstBuffer and push to queue */
            GstBuffer *buf = gst_buffer_new_allocate (NULL, sess->frag_len, NULL);
            GstMapInfo map;
            gst_buffer_map (buf, &map, GST_MAP_WRITE);
            memcpy (map.data, sess->frag_buf, sess->frag_len);
            gst_buffer_unmap (buf, &map);

            /* Leave PTS as GST_CLOCK_TIME_NONE; downstream decides timestamps */
            g_async_queue_push (self->queue, buf);

            /* Reset for next message */
            sess->frag_len = 0;
            /* Keep frag_buf allocated to reuse for next message */
        }
        break;
    }

    /* ---- Client connection rejected / failed ---- */
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        GST_ERROR_OBJECT (self, "wssrc client: connection error: %s",
                          in ? (const char *) in : "(no detail)");
        g_mutex_lock (&self->lock);
        self->wsi       = NULL;
        self->connected = FALSE;
        /* Wake start() so it can bail out instead of waiting the full timeout */
        g_cond_broadcast (&self->cond);
        g_mutex_unlock (&self->lock);
        g_async_queue_push (self->queue, gst_buffer_new ());
        break;

    /* ---- Connection closed ---- */
    case LWS_CALLBACK_CLOSED:
    case LWS_CALLBACK_CLIENT_CLOSED:
        if (sess && sess->frag_buf) {
            g_free (sess->frag_buf);
            sess->frag_buf = NULL;
        }
        GST_INFO_OBJECT (self, "wssrc: connection closed (reason=%d)", reason);
        g_mutex_lock (&self->lock);
        self->wsi       = NULL;
        self->connected = FALSE;
        if (self->mode == GST_WS_MODE_SERVER)
            self->server_has_client = FALSE;
        g_cond_broadcast (&self->cond);
        g_mutex_unlock (&self->lock);
        /* Push a sentinel to unblock create() */
        g_async_queue_push (self->queue, gst_buffer_new ());
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
lws_thread_src (gpointer data)
{
    GstWsSrc *self = (GstWsSrc *) data;

    GST_DEBUG_OBJECT (self, "LWS service thread started");

    while (self->thread_running) {
        lws_service (self->lws_ctx, 50 /* ms timeout */);
    }

    GST_DEBUG_OBJECT (self, "LWS service thread exiting");
    return NULL;
}

/* ================================================================
 * GstBaseSrc / GstPushSrc virtuals
 * ================================================================ */

static gboolean
gst_ws_src_negotiate (GstBaseSrc *bsrc)
{
    /* Query the downstream peer's accepted caps and fixate to the first
     * concrete option.  This lets the user put a caps filter or a decoder
     * (jpegdec, h264parse, etc.) directly after wssrc without an explicit
     * caps= argument, because we adopt whatever the peer expects.
     * If the peer accepts ANY (e.g. fakesink), we skip sending a caps event
     * so the pipeline doesn't choke on non-fixed caps. */
    GstPad  *srcpad    = GST_BASE_SRC_PAD (bsrc);
    GstCaps *peer_caps = gst_pad_peer_query_caps (srcpad, NULL);

    if (!peer_caps || gst_caps_is_any (peer_caps)) {
        if (peer_caps) gst_caps_unref (peer_caps);
        return TRUE; /* no caps event needed; GstBaseSrc handles stream-start/segment */
    }

    /* Fixate: resolve any ranges/lists to concrete values */
    GstCaps *fixed = gst_caps_fixate (gst_caps_copy (peer_caps));
    gst_caps_unref (peer_caps);

    gboolean ret = gst_base_src_set_caps (bsrc, fixed);
    gst_caps_unref (fixed);
    return ret;
}

static gboolean
gst_ws_src_start (GstBaseSrc *bsrc)
{
    GstWsSrc *self = GST_WS_SRC (bsrc);

    GST_DEBUG_OBJECT (self, "start: mode=%d", self->mode);

    self->flushing        = FALSE;
    self->connected       = FALSE;
    self->server_has_client = FALSE;

    static const struct lws_protocols protocols[] = {
        { "wsplugin", lws_callback_src, sizeof (WsSrcSession), 0, 0, NULL, 0 },
        /* "default" entry catches clients that send no Sec-WebSocket-Protocol header */
        { "default",  lws_callback_src, sizeof (WsSrcSession), 0, 0, NULL, 0 },
        LWS_PROTOCOL_LIST_TERM
    };

    struct lws_context_creation_info ctx_info;
    memset (&ctx_info, 0, sizeof ctx_info);
    ctx_info.protocols = protocols;
    ctx_info.user      = self;        /* accessible via lws_context_user() */
    ctx_info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    if (self->mode == GST_WS_MODE_SERVER) {
        ctx_info.port = self->port;
        GST_INFO_OBJECT (self, "wssrc server: listening on port %d", self->port);
    } else {
        ctx_info.port = CONTEXT_PORT_NO_LISTEN;
    }

    self->lws_ctx = lws_create_context (&ctx_info);
    if (!self->lws_ctx) {
        GST_ERROR_OBJECT (self, "Failed to create lws context");
        return FALSE;
    }

    /* Client mode: initiate connection */
    if (self->mode == GST_WS_MODE_CLIENT) {
        /* Parse ws://host:port/path */
        const char *uri_str = self->uri;
        char host[256] = {0};
        char path[512] = "/";
        int  port_num  = 80;
        int  use_ssl   = 0;

        /* Strip scheme */
        const char *rest = uri_str;
        if (g_str_has_prefix (uri_str, "wss://")) {
            use_ssl = LCCSCF_USE_SSL;
            rest    = uri_str + 6;
            port_num = 443;
        } else if (g_str_has_prefix (uri_str, "ws://")) {
            rest = uri_str + 5;
        }

        /* host[:port][/path] */
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
        if (slash) {
            g_strlcpy (path, slash, sizeof(path));
        }

        GST_INFO_OBJECT (self, "wssrc client: connecting to %s:%d%s", host, port_num, path);

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

        self->wsi = lws_client_connect_via_info (&conn_info);
        if (!self->wsi) {
            GST_ERROR_OBJECT (self, "lws_client_connect_via_info failed");
            lws_context_destroy (self->lws_ctx);
            self->lws_ctx = NULL;
            return FALSE;
        }
    }

    /* Start service thread */
    self->thread_running = TRUE;
    self->lws_thread = g_thread_new ("wssrc-lws", lws_thread_src, self);

    /* Wait for connection (with timeout) */
    gint64 deadline = g_get_monotonic_time () +
                      (gint64) self->connect_timeout * G_TIME_SPAN_SECOND;
    gboolean connect_ok = FALSE;
    g_mutex_lock (&self->lock);
    while (!self->connected && !self->flushing) {
        if (!g_cond_wait_until (&self->cond, &self->lock, deadline)) {
            GST_ERROR_OBJECT (self, "wssrc: connection timed out after %ds",
                              self->connect_timeout);
            break;
        }
        /* Cond fired but still not connected — error callback woke us early */
        if (!self->connected && !self->flushing) {
            GST_ERROR_OBJECT (self, "wssrc: connection failed (check server logs)");
            break;
        }
    }
    connect_ok = self->connected;
    g_mutex_unlock (&self->lock);

    if (!connect_ok) {
        self->thread_running = FALSE;
        lws_cancel_service (self->lws_ctx);
        g_thread_join (self->lws_thread);
        self->lws_thread = NULL;
        lws_context_destroy (self->lws_ctx);
        self->lws_ctx = NULL;
        return FALSE;
    }

    return TRUE;
}

static gboolean
gst_ws_src_stop (GstBaseSrc *bsrc)
{
    GstWsSrc *self = GST_WS_SRC (bsrc);

    GST_DEBUG_OBJECT (self, "stop");

    self->flushing = TRUE;

    if (self->lws_thread) {
        self->thread_running = FALSE;
        lws_cancel_service (self->lws_ctx);
        /* Unblock create() if it is sitting on the queue */
        g_async_queue_push (self->queue, gst_buffer_new ());
        g_thread_join (self->lws_thread);
        self->lws_thread = NULL;
    }

    if (self->lws_ctx) {
        lws_context_destroy (self->lws_ctx);
        self->lws_ctx = NULL;
    }

    self->wsi             = NULL;
    self->connected       = FALSE;
    self->server_has_client = FALSE;

    /* Drain any remaining items */
    GstBuffer *buf;
    while ((buf = g_async_queue_try_pop (self->queue)) != NULL)
        gst_buffer_unref (buf);

    return TRUE;
}

static gboolean
gst_ws_src_unlock (GstBaseSrc *bsrc)
{
    GstWsSrc *self = GST_WS_SRC (bsrc);
    GST_DEBUG_OBJECT (self, "unlock");
    g_mutex_lock (&self->lock);
    self->flushing = TRUE;
    g_cond_broadcast (&self->cond);
    g_mutex_unlock (&self->lock);
    /* Unblock g_async_queue_timeout_pop in create() */
    g_async_queue_push (self->queue, gst_buffer_new ());
    return TRUE;
}

static gboolean
gst_ws_src_unlock_stop (GstBaseSrc *bsrc)
{
    GstWsSrc *self = GST_WS_SRC (bsrc);
    GST_DEBUG_OBJECT (self, "unlock_stop");
    g_mutex_lock (&self->lock);
    self->flushing = FALSE;
    g_mutex_unlock (&self->lock);
    return TRUE;
}

static GstFlowReturn
gst_ws_src_create (GstPushSrc *psrc, GstBuffer **outbuf)
{
    GstWsSrc *self = GST_WS_SRC (psrc);

    while (TRUE) {
        if (self->flushing)
            return GST_FLOW_FLUSHING;

        /* Block up to 200ms so we can re-check flushing */
        GstBuffer *buf = g_async_queue_timeout_pop (self->queue,
                                                    200 * G_TIME_SPAN_MILLISECOND);
        if (!buf)
            continue; /* timeout — loop and check flushing */

        if (self->flushing) {
            gst_buffer_unref (buf);
            return GST_FLOW_FLUSHING;
        }

        /* A zero-size buffer is our sentinel for EOS/disconnect */
        if (gst_buffer_get_size (buf) == 0) {
            gst_buffer_unref (buf);
            if (!self->connected && !self->flushing) {
                GST_INFO_OBJECT (self, "wssrc: connection lost, sending EOS");
                return GST_FLOW_EOS;
            }
            continue;
        }

        *outbuf = buf;
        return GST_FLOW_OK;
    }
}
