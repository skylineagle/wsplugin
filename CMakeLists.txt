cmake_minimum_required(VERSION 3.16)
project(gstwsplugin C)

set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(LWS REQUIRED libwebsockets)
pkg_check_modules(OPENSSL REQUIRED openssl)

set(SOURCES
    src/gstwsplugin.c
    src/gstwssrc.c
    src/gstwssink.c
)

add_library(gstwsplugin MODULE ${SOURCES})

target_include_directories(gstwsplugin PRIVATE
    ${GST_INCLUDE_DIRS}
    ${GST_BASE_INCLUDE_DIRS}
    ${LWS_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    src
)

target_link_libraries(gstwsplugin PRIVATE
    ${GST_LIBRARIES}
    ${GST_BASE_LIBRARIES}
    ${LWS_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

target_link_directories(gstwsplugin PRIVATE
    ${GST_LIBRARY_DIRS}
    ${GST_BASE_LIBRARY_DIRS}
    ${LWS_LIBRARY_DIRS}
    ${OPENSSL_LIBRARY_DIRS}
)

target_compile_options(gstwsplugin PRIVATE
    ${GST_CFLAGS_OTHER}
    ${GST_BASE_CFLAGS_OTHER}
)

target_compile_definitions(gstwsplugin PRIVATE
    PACKAGE="gstwsplugin"
)

# Remove the 'lib' prefix so GStreamer can find it as 'gstwsplugin'
set_target_properties(gstwsplugin PROPERTIES PREFIX "")

# Install into the user GStreamer plugin directory
set(GST_PLUGIN_INSTALL_DIR "$ENV{HOME}/.local/share/gstreamer-1.0/plugins"
    CACHE PATH "GStreamer plugin install directory")

install(TARGETS gstwsplugin
    LIBRARY DESTINATION ${GST_PLUGIN_INSTALL_DIR}
)
