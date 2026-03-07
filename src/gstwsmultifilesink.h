#ifndef __GST_WS_MULTI_FILE_SINK_H__
#define __GST_WS_MULTI_FILE_SINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_WS_MULTI_FILE_SINK (gst_ws_multi_file_sink_get_type())
G_DECLARE_FINAL_TYPE (GstWsMultiFileSink, gst_ws_multi_file_sink, GST, WS_MULTI_FILE_SINK, GstBin)

GST_ELEMENT_REGISTER_DECLARE (wsmultifilesink);

G_END_DECLS

#endif
