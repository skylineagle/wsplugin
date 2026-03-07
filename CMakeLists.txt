cmake_minimum_required(VERSION 3.16)
project(gstwsplugin C)

set(CMAKE_C_STANDARD 11)

option(ENABLE_COVERAGE "Enable coverage instrumentation" OFF)

find_package(PkgConfig REQUIRED)
include(CTest)

pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(LWS REQUIRED libwebsockets)
pkg_check_modules(OPENSSL REQUIRED openssl)

if(ENABLE_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC.")
    endif()

    add_compile_options(-O0 -g --coverage)
    add_link_options(--coverage)
endif()

set(SOURCES
    src/gstwsplugin.c
    src/gstwsmultifilesink.c
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

set_target_properties(gstwsplugin PROPERTIES PREFIX "")

set(GST_PLUGIN_INSTALL_DIR "$ENV{HOME}/.local/share/gstreamer-1.0/plugins"
    CACHE PATH "GStreamer plugin install directory")

install(TARGETS gstwsplugin
    LIBRARY DESTINATION ${GST_PLUGIN_INSTALL_DIR}
)

if(BUILD_TESTING)
    add_executable(test_wssrc
        tests/test_wssrc.c
        tests/support/fake_lws.c
    )

    target_include_directories(test_wssrc PRIVATE
        ${GST_INCLUDE_DIRS}
        ${GST_BASE_INCLUDE_DIRS}
        tests/include
        src
    )

    target_link_libraries(test_wssrc PRIVATE
        ${GST_LIBRARIES}
        ${GST_BASE_LIBRARIES}
    )

    target_link_directories(test_wssrc PRIVATE
        ${GST_LIBRARY_DIRS}
        ${GST_BASE_LIBRARY_DIRS}
    )

    target_compile_options(test_wssrc PRIVATE
        ${GST_CFLAGS_OTHER}
        ${GST_BASE_CFLAGS_OTHER}
    )

    target_compile_definitions(test_wssrc PRIVATE
        PACKAGE="gstwsplugin"
    )

    add_executable(test_wssink
        tests/test_wssink.c
        tests/support/fake_lws.c
    )

    target_include_directories(test_wssink PRIVATE
        ${GST_INCLUDE_DIRS}
        ${GST_BASE_INCLUDE_DIRS}
        tests/include
        src
    )

    target_link_libraries(test_wssink PRIVATE
        ${GST_LIBRARIES}
        ${GST_BASE_LIBRARIES}
    )

    target_link_directories(test_wssink PRIVATE
        ${GST_LIBRARY_DIRS}
        ${GST_BASE_LIBRARY_DIRS}
    )

    target_compile_options(test_wssink PRIVATE
        ${GST_CFLAGS_OTHER}
        ${GST_BASE_CFLAGS_OTHER}
    )

    target_compile_definitions(test_wssink PRIVATE
        PACKAGE="gstwsplugin"
    )

    add_executable(test_wsmultifilesink
        tests/test_wsmultifilesink.c
    )

    target_include_directories(test_wsmultifilesink PRIVATE
        ${GST_INCLUDE_DIRS}
        ${GST_BASE_INCLUDE_DIRS}
        src
    )

    target_link_libraries(test_wsmultifilesink PRIVATE
        ${GST_LIBRARIES}
        ${GST_BASE_LIBRARIES}
    )

    target_link_directories(test_wsmultifilesink PRIVATE
        ${GST_LIBRARY_DIRS}
        ${GST_BASE_LIBRARY_DIRS}
    )

    target_compile_options(test_wsmultifilesink PRIVATE
        ${GST_CFLAGS_OTHER}
        ${GST_BASE_CFLAGS_OTHER}
    )

    target_compile_definitions(test_wsmultifilesink PRIVATE
        PACKAGE="gstwsplugin"
    )

    add_test(NAME test_wssrc COMMAND test_wssrc)
    add_test(NAME test_wssink COMMAND test_wssink)
    add_test(NAME test_wsmultifilesink COMMAND test_wsmultifilesink)

    if(ENABLE_COVERAGE)
        find_program(GCOVR_EXECUTABLE gcovr)

        if(GCOVR_EXECUTABLE)
            add_custom_target(coverage
                COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                COMMAND ${GCOVR_EXECUTABLE}
                    --root ${CMAKE_SOURCE_DIR}
                    --filter ${CMAKE_SOURCE_DIR}/src
                    --exclude-lines-by-pattern G_OBJECT_WARN_INVALID_PROPERTY_ID
                    --exclude-lines-by-pattern GST_PLUGIN_DEFINE
                    --exclude-lines-by-pattern GST_ELEMENT_REGISTER_DEFINE
                    --txt ${CMAKE_BINARY_DIR}/coverage.txt
                    --xml ${CMAKE_BINARY_DIR}/coverage.xml
                    --xml-pretty
                    --txt-summary
                    --fail-under-line 100
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                USES_TERMINAL
            )
        endif()
    endif()
endif()
