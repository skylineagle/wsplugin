#ifndef __GST_WS_SINK_H__
#define __GST_WS_SINK_H__

#include "gstwsplugin.h"
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_WS_SINK (gst_ws_sink_get_type())
G_DECLARE_FINAL_TYPE (GstWsSink, gst_ws_sink, GST, WS_SINK, GstBaseSink)

/* Per-client state kept in a linked list (server mode supports multiple clients) */
typedef struct _WsSinkClient {
    struct lws          *wsi;
    /* Queue of GBytes* pending write for this client */
    GAsyncQueue         *send_queue;
    /* Currently being written (borrowed from send_queue) */
    GBytes              *current_msg;
    gsize                current_offset;
} WsSinkClient;

struct _GstWsSink {
    GstBaseSink parent;

    /* Properties */
    GstWsMode   mode;
    gchar      *uri;
    gint        port;
    gint        connect_timeout;

    /* LWS state */
    struct lws_context  *lws_ctx;
    struct lws          *client_wsi;    /* client mode: the single connection */

    /* Server mode: list of WsSinkClient*, protected by lock */
    GList       *clients;

    gboolean     connected;  /* at least one client is connected */

    /* Threading */
    GThread     *lws_thread;
    gboolean     thread_running;
    gboolean     flushing;

    /* Protects clients list and connected flag */
    GMutex       lock;
    GCond        cond;
};

GST_ELEMENT_REGISTER_DECLARE (wssink);

G_END_DECLS

#endif /* __GST_WS_SINK_H__ */
