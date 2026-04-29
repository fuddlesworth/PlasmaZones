# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# plasmazones-shader-render — headless offscreen renderer for the
# bundled shader catalog.  Used by the documentation site
# (phosphor-works.github.io/plasmazones/shaders/) and anyone who
# needs reproducible shader previews without a display, compositor,
# or screen-capture tool.

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
)

# QRhi lives in QtGui's "rhi/" tree which is still a
# partially-private header set in Qt 6.11 (public API, versioned
# include path only exposed via GuiPrivate).  We need it to drive
# the offscreen QQuickRenderControl path — no public alternative.
find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    GuiPrivate
    QuickPrivate
)

set(shader_render_SRCS
    main.cpp
    renderer.h
    renderer.cpp
    layoutloader.h
    layoutloader.cpp
    metadataloader.h
    metadataloader.cpp
    audiomock.h
    audiomock.cpp
    encoder.h
    encoder.cpp
    colorutil.h
)

add_executable(plasmazones-shader-render ${shader_render_SRCS})

target_link_libraries(plasmazones-shader-render PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickPrivate
    PhosphorRendering
    PhosphorShell
    PhosphorAudio
)

target_compile_features(plasmazones-shader-render PRIVATE cxx_std_20)

# Don't install by default — this is a developer tool, not a
# user-facing binary.  Enable explicitly if a packager wants it.
option(INSTALL_SHADER_RENDER_TOOL
    "Install plasmazones-shader-render to ${CMAKE_INSTALL_BINDIR}" OFF)
if(INSTALL_SHADER_RENDER_TOOL)
    include(GNUInstallDirs)
    install(TARGETS plasmazones-shader-render
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ─────────────────────────────────────────────────────────────────
# Tests for the loaders. Pure data parsers — no GPU, no scene graph
# — so they run anywhere ctest does. Compiled only when tools are
# enabled to keep the bare default build small.
# ─────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Test)

    add_executable(test_shader_render_layoutloader
        tests/test_layoutloader.cpp
        layoutloader.cpp
        layoutloader.h)
    set_target_properties(test_shader_render_layoutloader PROPERTIES AUTOMOC ON)
    target_link_libraries(test_shader_render_layoutloader PRIVATE
        Qt6::Test Qt6::Core Qt6::Gui)
    target_compile_features(test_shader_render_layoutloader PRIVATE cxx_std_20)
    add_test(NAME test_shader_render_layoutloader COMMAND test_shader_render_layoutloader)

    add_executable(test_shader_render_metadataloader
        tests/test_metadataloader.cpp
        metadataloader.cpp
        metadataloader.h)
    set_target_properties(test_shader_render_metadataloader PROPERTIES AUTOMOC ON)
    target_link_libraries(test_shader_render_metadataloader PRIVATE
        Qt6::Test Qt6::Core Qt6::Gui)
    target_compile_features(test_shader_render_metadataloader PRIVATE cxx_std_20)
    add_test(NAME test_shader_render_metadataloader COMMAND test_shader_render_metadataloader)
endif()
