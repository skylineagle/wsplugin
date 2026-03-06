#ifndef __GST_WS_SRC_H__
#define __GST_WS_SRC_H__

#include "gstwsplugin.h"
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_TYPE_WS_SRC (gst_ws_src_get_type())
G_DECLARE_FINAL_TYPE (GstWsSrc, gst_ws_src, GST, WS_SRC, GstPushSrc)

struct _GstWsSrc {
    GstPushSrc parent;

    /* Properties */
    GstWsMode   mode;
    gchar      *uri;
    gint        port;
    gint        connect_timeout;

    /* LWS state */
    struct lws_context  *lws_ctx;
    struct lws          *wsi;           /* active connection (client or first server client) */
    gboolean             connected;
    gboolean             server_has_client; /* server mode: a client is already accepted */

    /* Threading */
    GThread     *lws_thread;
    gboolean     thread_running;

    /* Buffer queue: LWS thread -> GStreamer thread */
    GAsyncQueue *queue;

    /* Signals shutdown to the LWS thread */
    gboolean     flushing;

    /* Mutex protecting lws state fields */
    GMutex       lock;
    GCond        cond;
};

GST_ELEMENT_REGISTER_DECLARE (wssrc);

G_END_DECLS

#endif /* __GST_WS_SRC_H__ */
