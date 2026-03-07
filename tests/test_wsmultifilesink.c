#include <gst/gst.h>
#include <string.h>

typedef struct _FakeMultiFileSink {
    GstElement parent;
    gchar *location;
    gint index;
    gboolean post_messages;
    gint next_file;
    guint max_files;
    guint64 max_file_size;
    guint64 max_file_duration;
    gboolean aggregate_gops;
} FakeMultiFileSink;

typedef struct _FakeMultiFileSinkClass {
    GstElementClass parent_class;
} FakeMultiFileSinkClass;

static gboolean test_factory_make_should_fail = FALSE;
static gboolean test_fake_child_has_sink_pad = TRUE;
static gboolean test_ghost_pad_new_should_fail = FALSE;

enum {
    FAKE_PROP_0,
    FAKE_PROP_LOCATION,
    FAKE_PROP_INDEX,
    FAKE_PROP_POST_MESSAGES,
    FAKE_PROP_NEXT_FILE,
    FAKE_PROP_MAX_FILES,
    FAKE_PROP_MAX_FILE_SIZE,
    FAKE_PROP_MAX_FILE_DURATION,
    FAKE_PROP_AGGREGATE_GOPS,
};

static GstStaticPadTemplate fake_sink_template = GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

G_DEFINE_TYPE(FakeMultiFileSink, fake_multi_file_sink, GST_TYPE_ELEMENT)

static void
fake_multi_file_sink_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    FakeMultiFileSink *sink = (FakeMultiFileSink *) object;

    switch (prop_id) {
        case FAKE_PROP_LOCATION:
            g_free(sink->location);
            sink->location = g_value_dup_string(value);
            break;
        case FAKE_PROP_INDEX:
            sink->index = g_value_get_int(value);
            break;
        case FAKE_PROP_POST_MESSAGES:
            sink->post_messages = g_value_get_boolean(value);
            break;
        case FAKE_PROP_NEXT_FILE:
            sink->next_file = g_value_get_int(value);
            break;
        case FAKE_PROP_MAX_FILES:
            sink->max_files = g_value_get_uint(value);
            break;
        case FAKE_PROP_MAX_FILE_SIZE:
            sink->max_file_size = g_value_get_uint64(value);
            break;
        case FAKE_PROP_MAX_FILE_DURATION:
            sink->max_file_duration = g_value_get_uint64(value);
            break;
        case FAKE_PROP_AGGREGATE_GOPS:
            sink->aggregate_gops = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
fake_multi_file_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    FakeMultiFileSink *sink = (FakeMultiFileSink *) object;

    switch (prop_id) {
        case FAKE_PROP_LOCATION:
            g_value_set_string(value, sink->location);
            break;
        case FAKE_PROP_INDEX:
            g_value_set_int(value, sink->index);
            break;
        case FAKE_PROP_POST_MESSAGES:
            g_value_set_boolean(value, sink->post_messages);
            break;
        case FAKE_PROP_NEXT_FILE:
            g_value_set_int(value, sink->next_file);
            break;
        case FAKE_PROP_MAX_FILES:
            g_value_set_uint(value, sink->max_files);
            break;
        case FAKE_PROP_MAX_FILE_SIZE:
            g_value_set_uint64(value, sink->max_file_size);
            break;
        case FAKE_PROP_MAX_FILE_DURATION:
            g_value_set_uint64(value, sink->max_file_duration);
            break;
        case FAKE_PROP_AGGREGATE_GOPS:
            g_value_set_boolean(value, sink->aggregate_gops);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
fake_multi_file_sink_finalize(GObject *object)
{
    FakeMultiFileSink *sink = (FakeMultiFileSink *) object;

    g_free(sink->location);

    G_OBJECT_CLASS(fake_multi_file_sink_parent_class)->finalize(object);
}

static void
fake_multi_file_sink_class_init(FakeMultiFileSinkClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = fake_multi_file_sink_set_property;
    gobject_class->get_property = fake_multi_file_sink_get_property;
    gobject_class->finalize = fake_multi_file_sink_finalize;

    g_object_class_install_property(gobject_class, FAKE_PROP_LOCATION,
        g_param_spec_string("location", "location", "location", "%05d",
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, FAKE_PROP_INDEX,
        g_param_spec_int("index", "index", "index", 0, G_MAXINT, 0,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, FAKE_PROP_POST_MESSAGES,
        g_param_spec_boolean("post-messages", "post-messages", "post-messages", FALSE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, FAKE_PROP_NEXT_FILE,
        g_param_spec_int("next-file", "next-file", "next-file", 0, G_MAXINT, 0,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, FAKE_PROP_MAX_FILES,
        g_param_spec_uint("max-files", "max-files", "max-files", 0, G_MAXUINT, 0,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, FAKE_PROP_MAX_FILE_SIZE,
        g_param_spec_uint64("max-file-size", "max-file-size", "max-file-size", 0, G_MAXUINT64,
            G_GUINT64_CONSTANT(2 * 1024 * 1024 * 1024), G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, FAKE_PROP_MAX_FILE_DURATION,
        g_param_spec_uint64("max-file-duration", "max-file-duration", "max-file-duration", 0, G_MAXUINT64,
            GST_CLOCK_TIME_NONE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, FAKE_PROP_AGGREGATE_GOPS,
        g_param_spec_boolean("aggregate-gops", "aggregate-gops", "aggregate-gops", FALSE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_add_static_pad_template(element_class, &fake_sink_template);
}

static void
fake_multi_file_sink_init(FakeMultiFileSink *sink)
{
    sink->location = g_strdup("%05d");
    sink->index = 0;
    sink->post_messages = FALSE;
    sink->next_file = 0;
    sink->max_files = 0;
    sink->max_file_size = G_GUINT64_CONSTANT(2 * 1024 * 1024 * 1024);
    sink->max_file_duration = GST_CLOCK_TIME_NONE;
    sink->aggregate_gops = FALSE;

    if (test_fake_child_has_sink_pad) {
        GstPad *pad = gst_pad_new_from_static_template(&fake_sink_template, "sink");
        gst_element_add_pad(GST_ELEMENT(sink), pad);
    }
}

static GstElement *
fake_gst_element_factory_make(const gchar *factory_name, const gchar *name)
{
    if (g_strcmp0(factory_name, "multifilesink") != 0)
        return gst_element_factory_make(factory_name, name);

    if (test_factory_make_should_fail)
        return NULL;

    if (name)
        return GST_ELEMENT(g_object_new(fake_multi_file_sink_get_type(), "name", name, NULL));

    return GST_ELEMENT(g_object_new(fake_multi_file_sink_get_type(), NULL));
}

static GstPad *
fake_gst_ghost_pad_new(const gchar *name, GstPad *target)
{
    if (test_ghost_pad_new_should_fail)
        return NULL;

    return gst_ghost_pad_new(name, target);
}

static gboolean
allow_invalid_property_warning(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data)
{
    (void) log_domain;
    (void) log_level;
    (void) user_data;

    return g_strstr_len(message, -1, "invalid property id") == NULL;
}

#define gst_element_factory_make fake_gst_element_factory_make
#define gst_ghost_pad_new fake_gst_ghost_pad_new
#include "../src/gstwsmultifilesink.c"
#undef gst_element_factory_make
#undef gst_ghost_pad_new

static void
ensure_gstreamer(void)
{
    static gsize initialized = 0;

    if (g_once_init_enter(&initialized)) {
        gst_init(NULL, NULL);
        g_once_init_leave(&initialized, 1);
    }
}

static GstWsMultiFileSink *
create_sink(void)
{
    ensure_gstreamer();
    test_factory_make_should_fail = FALSE;
    test_fake_child_has_sink_pad = TRUE;
    test_ghost_pad_new_should_fail = FALSE;
    return GST_WS_MULTI_FILE_SINK(g_object_new(GST_TYPE_WS_MULTI_FILE_SINK, NULL));
}

static void
destroy_sink(GstWsMultiFileSink *sink)
{
    gst_object_unref(sink);
    test_factory_make_should_fail = FALSE;
    test_fake_child_has_sink_pad = TRUE;
    test_ghost_pad_new_should_fail = FALSE;
}

static FakeMultiFileSink *
get_child_sink(GstWsMultiFileSink *sink)
{
    return (FakeMultiFileSink *) gst_bin_get_by_name(GST_BIN(sink), "multifilesink");
}

static void
assert_date_segment(const gchar *value, gsize length)
{
    guint index;

    for (index = 0; index < length; index++)
        g_assert_true(g_ascii_isdigit(value[index]));
}

static void
test_defaults_and_properties(void)
{
    GstWsMultiFileSink *sink = create_sink();
    FakeMultiFileSink *child = get_child_sink(sink);
    gchar *location = NULL;
    gchar *location_template = (gchar *) 0x1;
    gboolean post_messages = TRUE;
    gboolean timestamp_utc = TRUE;
    gint index = -1;
    gint next_file = -1;
    guint max_files = 99;
    guint64 max_file_size = 1;
    guint64 max_file_duration = 2;
    gboolean aggregate_gops = TRUE;
    GstPad *pad;

    g_object_get(sink,
                 "location", &location,
                 "location-template", &location_template,
                 "index", &index,
                 "post-messages", &post_messages,
                 "next-file", &next_file,
                 "max-files", &max_files,
                 "max-file-size", &max_file_size,
                 "max-file-duration", &max_file_duration,
                 "aggregate-gops", &aggregate_gops,
                 "timestamp-utc", &timestamp_utc,
                 NULL);

    g_assert_cmpstr(location, ==, "%05d");
    g_assert_null(location_template);
    g_assert_cmpint(index, ==, 0);
    g_assert_false(post_messages);
    g_assert_cmpint(next_file, ==, GST_WS_MULTI_FILE_SINK_NEXT_BUFFER);
    g_assert_cmpuint(max_files, ==, 0);
    g_assert_cmpuint(max_file_size, ==, G_GUINT64_CONSTANT(2 * 1024 * 1024 * 1024));
    g_assert_cmpuint(max_file_duration, ==, GST_CLOCK_TIME_NONE);
    g_assert_false(aggregate_gops);
    g_assert_false(timestamp_utc);
    g_assert_true(gst_element_register_wsmultifilesink(NULL));

    g_assert_nonnull(child);
    g_assert_cmpstr(child->location, ==, "%05d");
    g_assert_true(child->post_messages);

    pad = gst_element_get_static_pad(GST_ELEMENT(sink), "sink");
    g_assert_nonnull(pad);
    gst_object_unref(pad);

    g_free(location);
    g_free(location_template);
    gst_object_unref(child);
    destroy_sink(sink);
}

static void
test_forwarded_properties(void)
{
    GstWsMultiFileSink *sink = create_sink();
    FakeMultiFileSink *child = get_child_sink(sink);
    gchar *location = NULL;
    gboolean post_messages = FALSE;
    gboolean aggregate_gops = FALSE;
    gint index = 0;
    gint next_file = 0;
    guint max_files = 0;
    guint64 max_file_size = 0;
    guint64 max_file_duration = 0;

    g_object_set(sink,
                 "location", "segment-%03d.ts",
                 "index", 7,
                 "post-messages", TRUE,
                 "next-file", GST_WS_MULTI_FILE_SINK_NEXT_MAX_DURATION,
                 "max-files", 4u,
                 "max-file-size", (guint64) 4096,
                 "max-file-duration", (guint64) 123456,
                 "aggregate-gops", TRUE,
                 NULL);

    g_object_get(sink,
                 "location", &location,
                 "index", &index,
                 "post-messages", &post_messages,
                 "next-file", &next_file,
                 "max-files", &max_files,
                 "max-file-size", &max_file_size,
                 "max-file-duration", &max_file_duration,
                 "aggregate-gops", &aggregate_gops,
                 NULL);

    g_assert_cmpstr(location, ==, "segment-%03d.ts");
    g_assert_cmpint(index, ==, 7);
    g_assert_true(post_messages);
    g_assert_cmpint(next_file, ==, GST_WS_MULTI_FILE_SINK_NEXT_MAX_DURATION);
    g_assert_cmpuint(max_files, ==, 4);
    g_assert_cmpuint(max_file_size, ==, 4096);
    g_assert_cmpuint(max_file_duration, ==, 123456);
    g_assert_true(aggregate_gops);

    g_assert_cmpstr(child->location, ==, "segment-%03d.ts");
    g_assert_cmpint(child->index, ==, 7);
    g_assert_true(child->post_messages);
    g_assert_cmpint(child->next_file, ==, GST_WS_MULTI_FILE_SINK_NEXT_MAX_DURATION);
    g_assert_cmpuint(child->max_files, ==, 4);
    g_assert_cmpuint(child->max_file_size, ==, 4096);
    g_assert_cmpuint(child->max_file_duration, ==, 123456);
    g_assert_true(child->aggregate_gops);

    g_free(location);
    gst_object_unref(child);
    destroy_sink(sink);
}

static void
test_template_expansion(void)
{
    GstWsMultiFileSink *sink = create_sink();
    FakeMultiFileSink *child = get_child_sink(sink);
    gchar *expanded;
    const gchar *prefix = "clips/{camera}-";
    const gchar *suffix = "-012.mkv";
    gsize prefix_len = strlen(prefix);
    gsize suffix_len = strlen(suffix);
    gchar *location_template = NULL;
    gboolean timestamp_utc = FALSE;

    g_object_set(sink,
                 "location-template", "clips/{{camera}}-{timestamp:%Y%m%d}-{index:03}.mkv",
                 "index", 12,
                 "timestamp-utc", TRUE,
                 NULL);

    expanded = g_strdup(child->location);
    g_assert_true(g_str_has_prefix(expanded, prefix));
    g_assert_true(g_str_has_suffix(expanded, suffix));
    assert_date_segment(expanded + prefix_len, 8);
    g_assert_cmpuint(strlen(expanded), ==, prefix_len + 8 + suffix_len);

    g_object_get(sink,
                 "location-template", &location_template,
                 "timestamp-utc", &timestamp_utc,
                 NULL);
    g_assert_cmpstr(location_template, ==, "clips/{{camera}}-{timestamp:%Y%m%d}-{index:03}.mkv");
    g_assert_true(timestamp_utc);

    g_free(expanded);
    g_free(location_template);
    gst_object_unref(child);
    destroy_sink(sink);
}

static void
test_template_helper_edges(void)
{
    GstWsMultiFileSink *sink = create_sink();
    gchar *formatted_index;
    gchar *expanded;

    formatted_index = gst_ws_multi_file_sink_format_index(5, NULL);
    g_assert_cmpstr(formatted_index, ==, "5");
    g_free(formatted_index);

    formatted_index = gst_ws_multi_file_sink_format_index(5, "abc");
    g_assert_cmpstr(formatted_index, ==, "5");
    g_free(formatted_index);

    g_free(sink->location_template);
    sink->location_template = g_strdup("literal-{unknown}-{{x}}-{index}-{index:04}-{timestamp:}");

    expanded = gst_ws_multi_file_sink_expand_template(sink, 5);

    g_assert_nonnull(expanded);
    g_assert_nonnull(strstr(expanded, "literal-{unknown}-{x}-5-0005-"));
    g_assert_false(strstr(expanded, "{timestamp:}") != NULL);

    g_free(expanded);

    g_free(sink->location_template);
    sink->location_template = NULL;
    g_assert_null(gst_ws_multi_file_sink_expand_template(sink, 5));

    sink->location_template = g_strdup("{timestamp}");
    expanded = gst_ws_multi_file_sink_expand_template(sink, 5);
    g_assert_nonnull(expanded);
    g_assert_cmpuint(strlen(expanded), ==, strlen("20260307T231053"));
    g_free(expanded);

    sink->location_template = g_strdup("{timestamp:%Q}");
    expanded = gst_ws_multi_file_sink_expand_template(sink, 5);
    g_assert_nonnull(expanded);
    g_assert_cmpuint(strlen(expanded), ==, strlen("20260307T231053"));
    g_free(expanded);

    sink->location_template = g_strdup("prefix-{index");
    expanded = gst_ws_multi_file_sink_expand_template(sink, 5);
    g_assert_cmpstr(expanded, ==, "prefix-{index");
    g_free(expanded);

    destroy_sink(sink);
}

static void
test_clear_template_restores_location_pattern(void)
{
    GstWsMultiFileSink *sink = create_sink();
    FakeMultiFileSink *child = get_child_sink(sink);

    g_object_set(sink,
                 "location", "roll-%02d.bin",
                 "location-template", "file-{index:02}.bin",
                 NULL);
    g_assert_cmpstr(child->location, ==, "file-00.bin");

    g_object_set(sink, "location-template", "", NULL);
    g_assert_cmpstr(child->location, ==, "roll-%02d.bin");

    gst_object_unref(child);
    destroy_sink(sink);
}

static void
test_handle_message_reposts_when_enabled(void)
{
    GstElement *pipeline;
    GstBus *bus;
    GstWsMultiFileSink *sink = create_sink();
    FakeMultiFileSink *child = get_child_sink(sink);
    GstMessage *message;
    GstMessage *forwarded;
    const GstStructure *structure;

    pipeline = gst_pipeline_new("test-pipeline");
    gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(gst_object_ref(sink)));
    bus = gst_element_get_bus(pipeline);

    g_object_set(sink,
                 "post-messages", TRUE,
                 "location-template", "file-{index:02}.bin",
                 NULL);

    message = gst_message_new_element(GST_OBJECT(child),
        gst_structure_new("GstMultiFileSink",
                          "filename", G_TYPE_STRING, "file-00.bin",
                          "index", G_TYPE_INT, 0,
                          NULL));
    gst_ws_multi_file_sink_handle_message(GST_BIN(sink), message);

    g_assert_cmpstr(child->location, ==, "file-01.bin");

    forwarded = gst_bus_timed_pop_filtered(bus, 0, GST_MESSAGE_ELEMENT);
    g_assert_nonnull(forwarded);
    g_assert_true(GST_MESSAGE_SRC(forwarded) == GST_OBJECT(sink));

    structure = gst_message_get_structure(forwarded);
    g_assert_nonnull(structure);
    g_assert_true(gst_structure_has_name(structure, "GstMultiFileSink"));
    gst_message_unref(forwarded);

    gst_object_unref(bus);
    gst_object_unref(child);
    gst_object_unref(pipeline);
    destroy_sink(sink);
}

static void
test_handle_message_swallow_and_passthrough(void)
{
    GstElement *pipeline;
    GstBus *bus;
    GstWsMultiFileSink *sink = create_sink();
    FakeMultiFileSink *child = get_child_sink(sink);
    GstMessage *message;
    GstMessage *other_message;

    pipeline = gst_pipeline_new("test-pipeline");
    gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(gst_object_ref(sink)));
    bus = gst_element_get_bus(pipeline);

    g_object_set(sink, "location-template", "chunk-{index}.bin", NULL);

    message = gst_message_new_element(GST_OBJECT(child),
        gst_structure_new_empty("GstMultiFileSink"));
    gst_ws_multi_file_sink_handle_message(GST_BIN(sink), message);

    g_assert_cmpstr(child->location, ==, "chunk-1.bin");
    g_assert_null(gst_bus_timed_pop_filtered(bus, 0, GST_MESSAGE_ELEMENT));

    other_message = gst_message_new_element(GST_OBJECT(sink),
        gst_structure_new_empty("OtherMessage"));
    gst_ws_multi_file_sink_handle_message(GST_BIN(sink), other_message);

    gst_object_unref(bus);
    gst_object_unref(child);
    gst_object_unref(pipeline);
    destroy_sink(sink);
}

static void
test_factory_failure_and_invalid_properties(void)
{
    GstWsMultiFileSink *sink;
    GValue value = G_VALUE_INIT;
    GParamSpec *pspec = g_param_spec_int("invalid", "invalid", "invalid", 0, 1, 0, G_PARAM_READWRITE);

    ensure_gstreamer();
    test_factory_make_should_fail = TRUE;
    sink = GST_WS_MULTI_FILE_SINK(g_object_new(GST_TYPE_WS_MULTI_FILE_SINK, NULL));

    g_assert_null(sink->multifilesink);
    g_assert_null(gst_element_get_static_pad(GST_ELEMENT(sink), "sink"));

    g_object_set(sink,
                 "location", "",
                 "location-template", "chunk-{index}.bin",
                 "next-file", GST_WS_MULTI_FILE_SINK_NEXT_KEY_FRAME,
                 "max-files", 2u,
                 "max-file-size", (guint64) 22,
                 "max-file-duration", (guint64) 33,
                 "aggregate-gops", TRUE,
                 NULL);

    g_value_init(&value, G_TYPE_INT);
    g_value_set_int(&value, 1);

    g_test_log_set_fatal_handler(allow_invalid_property_warning, NULL);
    gst_ws_multi_file_sink_set_property(G_OBJECT(sink), 999, &value, pspec);
    gst_ws_multi_file_sink_get_property(G_OBJECT(sink), 999, &value, pspec);
    g_test_log_set_fatal_handler(NULL, NULL);

    g_value_unset(&value);
    g_param_spec_unref(pspec);
    destroy_sink(sink);
}

static void
test_child_without_sink_pad(void)
{
    GstWsMultiFileSink *sink;

    ensure_gstreamer();
    test_fake_child_has_sink_pad = FALSE;
    sink = GST_WS_MULTI_FILE_SINK(g_object_new(GST_TYPE_WS_MULTI_FILE_SINK, NULL));

    g_assert_nonnull(sink->multifilesink);
    g_assert_null(gst_element_get_static_pad(GST_ELEMENT(sink), "sink"));

    destroy_sink(sink);
}

static void
test_ghost_pad_creation_failure(void)
{
    GstWsMultiFileSink *sink;

    ensure_gstreamer();
    test_ghost_pad_new_should_fail = TRUE;
    sink = GST_WS_MULTI_FILE_SINK(g_object_new(GST_TYPE_WS_MULTI_FILE_SINK, NULL));

    g_assert_nonnull(sink->multifilesink);
    g_assert_null(gst_element_get_static_pad(GST_ELEMENT(sink), "sink"));

    destroy_sink(sink);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/wsmultifilesink/defaults-and-properties", test_defaults_and_properties);
    g_test_add_func("/wsmultifilesink/forwarded-properties", test_forwarded_properties);
    g_test_add_func("/wsmultifilesink/template-expansion", test_template_expansion);
    g_test_add_func("/wsmultifilesink/template-helper-edges", test_template_helper_edges);
    g_test_add_func("/wsmultifilesink/clear-template-restores-location-pattern", test_clear_template_restores_location_pattern);
    g_test_add_func("/wsmultifilesink/handle-message-reposts-when-enabled", test_handle_message_reposts_when_enabled);
    g_test_add_func("/wsmultifilesink/handle-message-swallow-and-passthrough", test_handle_message_swallow_and_passthrough);
    g_test_add_func("/wsmultifilesink/factory-failure-and-invalid-properties", test_factory_failure_and_invalid_properties);
    g_test_add_func("/wsmultifilesink/child-without-sink-pad", test_child_without_sink_pad);
    g_test_add_func("/wsmultifilesink/ghost-pad-creation-failure", test_ghost_pad_creation_failure);

    return g_test_run();
}
