#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwsplugin.h"
#include "gstwsmultifilesink.h"
#include "gstwssrc.h"
#include "gstwssink.h"

/* GstWsMode GType */
GType
gst_ws_mode_get_type (void)
{
    static GType type = 0;
    static const GEnumValue modes[] = {
        { GST_WS_MODE_CLIENT, "Client: connect to a remote WebSocket server", "client" },
        { GST_WS_MODE_SERVER, "Server: listen for incoming WebSocket connections", "server" },
        { 0, NULL, NULL },
    };

    if (g_once_init_enter (&type)) {
        GType t = g_enum_register_static ("GstWsMode", modes);
        g_once_init_leave (&type, t);
    }
    return type;
}

/* Plugin entry point */
static gboolean
plugin_init (GstPlugin * plugin)
{
    gboolean ret = TRUE;

    ret &= GST_ELEMENT_REGISTER (wsmultifilesink, plugin);
    ret &= GST_ELEMENT_REGISTER (wssrc, plugin);
    ret &= GST_ELEMENT_REGISTER (wssink, plugin);

    return ret;
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    wsplugin,
    "WebSocket source, sink, and enhanced multifile sink elements",
    plugin_init,
    "1.0.0",
    "LGPL",
    "wsplugin",
    "https://github.com/anomalyco/wsplugin"
)
