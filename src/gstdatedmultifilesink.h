#ifndef __GST_DATED_MULTI_FILE_SINK_H__
#define __GST_DATED_MULTI_FILE_SINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_DATED_MULTI_FILE_SINK (gst_dated_multi_file_sink_get_type())
G_DECLARE_FINAL_TYPE (GstDatedMultiFileSink, gst_dated_multi_file_sink, GST, DATED_MULTI_FILE_SINK, GstBin)

GST_ELEMENT_REGISTER_DECLARE (datedmultifilesink);

G_END_DECLS

#endif
