#ifndef __GST_WS_PLUGIN_H__
#define __GST_WS_PLUGIN_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/base/gstpushsrc.h>
#include <libwebsockets.h>

G_BEGIN_DECLS

/* Shared mode enum used by both wssrc and wssink */
typedef enum {
    GST_WS_MODE_CLIENT = 0,
    GST_WS_MODE_SERVER = 1,
} GstWsMode;

#define GST_TYPE_WS_MODE (gst_ws_mode_get_type())
GType gst_ws_mode_get_type (void);

/* Default values */
#define GST_WS_DEFAULT_MODE            GST_WS_MODE_CLIENT
#define GST_WS_DEFAULT_URI             "ws://localhost:8765"
#define GST_WS_DEFAULT_PORT            8765
#define GST_WS_DEFAULT_CONNECT_TIMEOUT 10

/* LWS ring buffer size for pre-padding (LWS requires LWS_PRE bytes before payload) */
#define GST_WS_LWS_SEND_HEADROOM       LWS_PRE

G_END_DECLS

#endif /* __GST_WS_PLUGIN_H__ */
