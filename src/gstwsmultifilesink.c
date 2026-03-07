#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstwsmultifilesink.h"

GST_DEBUG_CATEGORY_STATIC (gst_dated_multi_file_sink_debug);
#define GST_CAT_DEFAULT gst_dated_multi_file_sink_debug

#define DEFAULT_LOCATION "%05d"
#define DEFAULT_INDEX 0
#define DEFAULT_POST_MESSAGES FALSE
#define DEFAULT_NEXT_FILE GST_DATED_MULTI_FILE_SINK_NEXT_BUFFER
#define DEFAULT_MAX_FILES 0
#define DEFAULT_MAX_FILE_SIZE G_GUINT64_CONSTANT (2 * 1024 * 1024 * 1024)
#define DEFAULT_MAX_FILE_DURATION GST_CLOCK_TIME_NONE
#define DEFAULT_AGGREGATE_GOPS FALSE
#define DEFAULT_TIMESTAMP_UTC FALSE
#define DEFAULT_TIMESTAMP_FORMAT "%Y%m%dT%H%M%S"

typedef enum {
    GST_DATED_MULTI_FILE_SINK_NEXT_BUFFER,
    GST_DATED_MULTI_FILE_SINK_NEXT_DISCONT,
    GST_DATED_MULTI_FILE_SINK_NEXT_KEY_FRAME,
    GST_DATED_MULTI_FILE_SINK_NEXT_KEY_UNIT_EVENT,
    GST_DATED_MULTI_FILE_SINK_NEXT_MAX_SIZE,
    GST_DATED_MULTI_FILE_SINK_NEXT_MAX_DURATION
} GstDatedMultiFileSinkNext;

struct _GstDatedMultiFileSink {
    GstBin parent;
    GstElement *multifilesink;
    gchar *location;
    gchar *location_template;
    gboolean post_messages;
    gboolean timestamp_utc;
};

enum {
    PROP_0,
    PROP_LOCATION,
    PROP_INDEX,
    PROP_POST_MESSAGES,
    PROP_NEXT_FILE,
    PROP_MAX_FILES,
    PROP_MAX_FILE_SIZE,
    PROP_MAX_FILE_DURATION,
    PROP_AGGREGATE_GOPS,
    PROP_LOCATION_TEMPLATE,
    PROP_TIMESTAMP_UTC,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

#define GST_TYPE_DATED_MULTI_FILE_SINK_NEXT (gst_dated_multi_file_sink_next_get_type ())

static GType gst_dated_multi_file_sink_next_get_type (void);
static void gst_dated_multi_file_sink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_dated_multi_file_sink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_dated_multi_file_sink_finalize (GObject *object);
static void gst_dated_multi_file_sink_handle_message (GstBin *bin, GstMessage *message);

#define gst_dated_multi_file_sink_parent_class parent_class
G_DEFINE_TYPE (GstDatedMultiFileSink, gst_dated_multi_file_sink, GST_TYPE_BIN);
GST_ELEMENT_REGISTER_DEFINE (datedmultifilesink, "datedmultifilesink", GST_RANK_NONE, GST_TYPE_DATED_MULTI_FILE_SINK);

static GType
gst_dated_multi_file_sink_next_get_type (void)
{
    static GType type = 0;
    static const GEnumValue values[] = {
        { GST_DATED_MULTI_FILE_SINK_NEXT_BUFFER, "New file for each buffer", "buffer" },
        { GST_DATED_MULTI_FILE_SINK_NEXT_DISCONT, "New file after each discontinuity", "discont" },
        { GST_DATED_MULTI_FILE_SINK_NEXT_KEY_FRAME, "New file at each key frame", "key-frame" },
        { GST_DATED_MULTI_FILE_SINK_NEXT_KEY_UNIT_EVENT, "New file after a force key unit event", "key-unit-event" },
        { GST_DATED_MULTI_FILE_SINK_NEXT_MAX_SIZE, "New file when the configured maximum file size would be exceeded", "max-size" },
        { GST_DATED_MULTI_FILE_SINK_NEXT_MAX_DURATION, "New file when the configured maximum file duration would be exceeded", "max-duration" },
        { 0, NULL, NULL },
    };

    if (g_once_init_enter (&type)) {
        GType registered_type = g_enum_register_static ("GstDatedMultiFileSinkNext", values);
        g_once_init_leave (&type, registered_type);
    }

    return type;
}

static gchar *
gst_dated_multi_file_sink_format_index (gint index, const gchar *format)
{
    gint width = 0;
    const gchar *effective_format = format;

    if (!effective_format || !*effective_format)
        return g_strdup_printf ("%d", index);

    width = (gint) g_ascii_strtoll (effective_format, NULL, 10);
    if (width <= 0)
        return g_strdup_printf ("%d", index);

    return g_strdup_printf ("%0*d", width, index);
}

static gboolean
gst_dated_multi_file_sink_append_token (GString *output, const gchar *token, gint index, GDateTime *date_time)
{
    gchar *formatted = NULL;

    if (g_str_equal (token, "index")) {
        g_string_append_printf (output, "%d", index);
        return TRUE;
    }

    if (g_str_has_prefix (token, "index:")) {
        formatted = gst_dated_multi_file_sink_format_index (index, token + strlen ("index:"));
        g_string_append (output, formatted);
        g_free (formatted);
        return TRUE;
    }

    if (g_str_equal (token, "timestamp")) {
        formatted = g_date_time_format (date_time, DEFAULT_TIMESTAMP_FORMAT);
        g_string_append (output, formatted);
        g_free (formatted);
        return TRUE;
    }

    if (g_str_has_prefix (token, "timestamp:")) {
        const gchar *timestamp_format = token + strlen ("timestamp:");

        if (!*timestamp_format)
            timestamp_format = DEFAULT_TIMESTAMP_FORMAT;

        formatted = g_date_time_format (date_time, timestamp_format);
        if (!formatted)
            formatted = g_date_time_format (date_time, DEFAULT_TIMESTAMP_FORMAT);
        g_string_append (output, formatted);
        g_free (formatted);
        return TRUE;
    }

    return FALSE;
}

static gchar *
gst_dated_multi_file_sink_expand_template (GstDatedMultiFileSink *self, gint index)
{
    GDateTime *date_time;
    GString *output;
    const gchar *cursor;

    if (!self->location_template || !*self->location_template)
        return NULL;

    date_time = self->timestamp_utc ? g_date_time_new_now_utc () : g_date_time_new_now_local ();

    output = g_string_new (NULL);
    cursor = self->location_template;

    while (*cursor) {
        const gchar *token_end;
        gchar *token;

        if (cursor[0] == '{' && cursor[1] == '{') {
            g_string_append_c (output, '{');
            cursor += 2;
            continue;
        }

        if (cursor[0] == '}' && cursor[1] == '}') {
            g_string_append_c (output, '}');
            cursor += 2;
            continue;
        }

        if (*cursor != '{') {
            g_string_append_c (output, *cursor);
            cursor++;
            continue;
        }

        token_end = strchr (cursor + 1, '}');
        if (!token_end) {
            g_string_append (output, cursor);
            break;
        }

        token = g_strndup (cursor + 1, token_end - cursor - 1);
        if (!gst_dated_multi_file_sink_append_token (output, token, index, date_time))
            g_string_append_len (output, cursor, token_end - cursor + 1);
        g_free (token);
        cursor = token_end + 1;
    }

    g_date_time_unref (date_time);
    return g_string_free (output, FALSE);
}

static gint
gst_dated_multi_file_sink_get_child_index (GstDatedMultiFileSink *self)
{
    gint index = DEFAULT_INDEX;

    if (self->multifilesink)
        g_object_get (self->multifilesink, "index", &index, NULL);

    return index;
}

static void
gst_dated_multi_file_sink_refresh_location (GstDatedMultiFileSink *self, gint index)
{
    gchar *expanded_location;

    if (!self->multifilesink)
        return;

    if (!self->location_template || !*self->location_template) {
        g_object_set (self->multifilesink,
                      "location", self->location ? self->location : DEFAULT_LOCATION,
                      NULL);
        return;
    }

    expanded_location = gst_dated_multi_file_sink_expand_template (self, index);
    g_object_set (self->multifilesink, "location", expanded_location, NULL);
    g_free (expanded_location);
}

static void
gst_dated_multi_file_sink_class_init (GstDatedMultiFileSinkClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
    GstBinClass *bin_class = GST_BIN_CLASS (klass);

    GST_DEBUG_CATEGORY_INIT (gst_dated_multi_file_sink_debug, "datedmultifilesink", 0, "Dated multi-file sink");

    gobject_class->set_property = gst_dated_multi_file_sink_set_property;
    gobject_class->get_property = gst_dated_multi_file_sink_get_property;
    gobject_class->finalize = gst_dated_multi_file_sink_finalize;

    bin_class->handle_message = gst_dated_multi_file_sink_handle_message;

    g_object_class_install_property (gobject_class, PROP_LOCATION,
        g_param_spec_string ("location", "File Location",
            "Location pattern used by the wrapped multifilesink when no location-template is set",
            DEFAULT_LOCATION,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_INDEX,
        g_param_spec_int ("index", "Index",
            "Index to use when generating file names",
            0, G_MAXINT, DEFAULT_INDEX,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_POST_MESSAGES,
        g_param_spec_boolean ("post-messages", "Post Messages",
            "Post a message from the wrapper element after each completed file",
            DEFAULT_POST_MESSAGES,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_NEXT_FILE,
        g_param_spec_enum ("next-file", "Next File",
            "When to start a new file",
            GST_TYPE_DATED_MULTI_FILE_SINK_NEXT, DEFAULT_NEXT_FILE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_MAX_FILES,
        g_param_spec_uint ("max-files", "Max Files",
            "Maximum number of files to keep on disk",
            0, G_MAXUINT, DEFAULT_MAX_FILES,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_MAX_FILE_SIZE,
        g_param_spec_uint64 ("max-file-size", "Max File Size",
            "Maximum file size before rolling to the next file",
            0, G_MAXUINT64, DEFAULT_MAX_FILE_SIZE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_MAX_FILE_DURATION,
        g_param_spec_uint64 ("max-file-duration", "Max File Duration",
            "Maximum file duration before rolling to the next file",
            0, G_MAXUINT64, DEFAULT_MAX_FILE_DURATION,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_AGGREGATE_GOPS,
        g_param_spec_boolean ("aggregate-gops", "Aggregate GOPs",
            "Aggregate complete GOPs before writing a new file",
            DEFAULT_AGGREGATE_GOPS,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_LOCATION_TEMPLATE,
        g_param_spec_string ("location-template", "Location Template",
            "Template for generated paths. Supports {timestamp[:format]} and {index[:width]} tokens",
            NULL,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_TIMESTAMP_UTC,
        g_param_spec_boolean ("timestamp-utc", "Timestamp UTC",
            "Use UTC instead of local time when expanding location-template timestamps",
            DEFAULT_TIMESTAMP_UTC,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata (element_class,
        "Enhanced Multi-file Sink",
        "Sink/File",
        "Wraps multifilesink and adds timestamp-aware file naming",
        "wsplugin");

    gst_element_class_add_static_pad_template (element_class, &sink_template);
}

static void
gst_dated_multi_file_sink_init (GstDatedMultiFileSink *self)
{
    GstPad *sink_pad;
    GstPad *ghost_pad;

    self->location = g_strdup (DEFAULT_LOCATION);
    self->location_template = NULL;
    self->post_messages = DEFAULT_POST_MESSAGES;
    self->timestamp_utc = DEFAULT_TIMESTAMP_UTC;
    self->multifilesink = gst_element_factory_make ("multifilesink", "multifilesink");

    if (!self->multifilesink) {
        GST_ERROR_OBJECT (self, "Failed to create wrapped multifilesink element");
        return;
    }

    gst_bin_add (GST_BIN (self), self->multifilesink);
    g_object_set (self->multifilesink,
                  "location", self->location,
                  "post-messages", TRUE,
                  NULL);

    sink_pad = gst_element_get_static_pad (self->multifilesink, "sink");
    if (!sink_pad) {
        GST_ERROR_OBJECT (self, "Wrapped multifilesink does not expose a sink pad");
        return;
    }

    ghost_pad = gst_ghost_pad_new ("sink", sink_pad);
    gst_object_unref (sink_pad);

    if (!ghost_pad) {
        GST_ERROR_OBJECT (self, "Failed to create ghost sink pad");
        return;
    }

    gst_pad_set_active (ghost_pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (self), ghost_pad);
    gst_dated_multi_file_sink_refresh_location (self, DEFAULT_INDEX);
}

static void
gst_dated_multi_file_sink_finalize (GObject *object)
{
    GstDatedMultiFileSink *self = GST_DATED_MULTI_FILE_SINK (object);

    g_free (self->location);
    g_free (self->location_template);

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dated_multi_file_sink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstDatedMultiFileSink *self = GST_DATED_MULTI_FILE_SINK (object);

    switch (prop_id) {
        case PROP_LOCATION:
            g_free (self->location);
            self->location = g_strdup (g_value_get_string (value));
            if (!self->location || !*self->location) {
                g_free (self->location);
                self->location = g_strdup (DEFAULT_LOCATION);
            }
            if (!self->location_template || !*self->location_template)
                gst_dated_multi_file_sink_refresh_location (self, gst_dated_multi_file_sink_get_child_index (self));
            break;
        case PROP_INDEX:
            if (self->multifilesink)
                g_object_set (self->multifilesink, "index", g_value_get_int (value), NULL);
            gst_dated_multi_file_sink_refresh_location (self, g_value_get_int (value));
            break;
        case PROP_POST_MESSAGES:
            self->post_messages = g_value_get_boolean (value);
            break;
        case PROP_NEXT_FILE:
            if (self->multifilesink)
                g_object_set (self->multifilesink, "next-file", g_value_get_enum (value), NULL);
            break;
        case PROP_MAX_FILES:
            if (self->multifilesink)
                g_object_set (self->multifilesink, "max-files", g_value_get_uint (value), NULL);
            break;
        case PROP_MAX_FILE_SIZE:
            if (self->multifilesink)
                g_object_set (self->multifilesink, "max-file-size", g_value_get_uint64 (value), NULL);
            break;
        case PROP_MAX_FILE_DURATION:
            if (self->multifilesink)
                g_object_set (self->multifilesink, "max-file-duration", g_value_get_uint64 (value), NULL);
            break;
        case PROP_AGGREGATE_GOPS:
            if (self->multifilesink)
                g_object_set (self->multifilesink, "aggregate-gops", g_value_get_boolean (value), NULL);
            break;
        case PROP_LOCATION_TEMPLATE:
            g_free (self->location_template);
            self->location_template = g_value_dup_string (value);
            if (self->location_template && !*self->location_template) {
                g_free (self->location_template);
                self->location_template = NULL;
            }
            gst_dated_multi_file_sink_refresh_location (self, gst_dated_multi_file_sink_get_child_index (self));
            break;
        case PROP_TIMESTAMP_UTC:
            self->timestamp_utc = g_value_get_boolean (value);
            if (self->location_template && *self->location_template)
                gst_dated_multi_file_sink_refresh_location (self, gst_dated_multi_file_sink_get_child_index (self));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gst_dated_multi_file_sink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstDatedMultiFileSink *self = GST_DATED_MULTI_FILE_SINK (object);
    gint next_file = DEFAULT_NEXT_FILE;
    guint max_files = DEFAULT_MAX_FILES;
    guint64 max_file_size = DEFAULT_MAX_FILE_SIZE;
    guint64 max_file_duration = DEFAULT_MAX_FILE_DURATION;
    gboolean aggregate_gops = DEFAULT_AGGREGATE_GOPS;

    switch (prop_id) {
        case PROP_LOCATION:
            g_value_set_string (value, self->location);
            break;
        case PROP_INDEX:
            g_value_set_int (value, gst_dated_multi_file_sink_get_child_index (self));
            break;
        case PROP_POST_MESSAGES:
            g_value_set_boolean (value, self->post_messages);
            break;
        case PROP_NEXT_FILE:
            if (self->multifilesink)
                g_object_get (self->multifilesink, "next-file", &next_file, NULL);
            g_value_set_enum (value, next_file);
            break;
        case PROP_MAX_FILES:
            if (self->multifilesink)
                g_object_get (self->multifilesink, "max-files", &max_files, NULL);
            g_value_set_uint (value, max_files);
            break;
        case PROP_MAX_FILE_SIZE:
            if (self->multifilesink)
                g_object_get (self->multifilesink, "max-file-size", &max_file_size, NULL);
            g_value_set_uint64 (value, max_file_size);
            break;
        case PROP_MAX_FILE_DURATION:
            if (self->multifilesink)
                g_object_get (self->multifilesink, "max-file-duration", &max_file_duration, NULL);
            g_value_set_uint64 (value, max_file_duration);
            break;
        case PROP_AGGREGATE_GOPS:
            if (self->multifilesink)
                g_object_get (self->multifilesink, "aggregate-gops", &aggregate_gops, NULL);
            g_value_set_boolean (value, aggregate_gops);
            break;
        case PROP_LOCATION_TEMPLATE:
            g_value_set_string (value, self->location_template);
            break;
        case PROP_TIMESTAMP_UTC:
            g_value_set_boolean (value, self->timestamp_utc);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gst_dated_multi_file_sink_handle_message (GstBin *bin, GstMessage *message)
{
    GstDatedMultiFileSink *self = GST_DATED_MULTI_FILE_SINK (bin);
    const GstStructure *structure = gst_message_get_structure (message);
    gint current_index;

    if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT ||
        GST_MESSAGE_SRC (message) != GST_OBJECT (self->multifilesink) ||
        !structure ||
        !gst_structure_has_name (structure, "GstMultiFileSink")) {
        GST_BIN_CLASS (parent_class)->handle_message (bin, message);
        return;
    }

    current_index = gst_dated_multi_file_sink_get_child_index (self);
    if (!gst_structure_get_int (structure, "index", &current_index))
        current_index = gst_dated_multi_file_sink_get_child_index (self);

    gst_dated_multi_file_sink_refresh_location (self, current_index + 1);

    if (self->post_messages) {
        gst_element_post_message (GST_ELEMENT (self),
                                  gst_message_new_element (GST_OBJECT (self),
                                                           gst_structure_copy (structure)));
    }

    gst_message_unref (message);
}
