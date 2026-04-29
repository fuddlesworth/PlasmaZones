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
    PhosphorWayland
)

# The tool reuses the daemon's ZoneUniformExtension header so the
# UBO layout matches the runtime exactly.  This is intentional
# coupling — if/when the extension moves into a shared library
# (probably libs/phosphor-wayland), drop this include and add the
# library to target_link_libraries above.
target_include_directories(plasmazones-shader-render PRIVATE
    ${CMAKE_SOURCE_DIR}/src/daemon/rendering
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
