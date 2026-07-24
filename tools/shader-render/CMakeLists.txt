# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# plasmazones-shader-render — headless offscreen renderer for the
# bundled shader catalog.  Used by the documentation site
# (phosphor-works.github.io/plasmazones/overlays/) and anyone who
# needs reproducible shader previews without a display, compositor,
# or screen-capture tool.

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
)

# QRhi lives in QtGui's "rhi/" tree which is still a partially-private
# header set in Qt 6.11 (public API, versioned include path only exposed
# via GuiPrivate). We need it to drive the offscreen QQuickRenderControl
# path — no public alternative.
#
# Fedora ships `Qt6::GuiPrivate` / `Qt6::QuickPrivate` as targets defined
# inside Qt6GuiConfig / Qt6QuickConfig rather than as standalone
# Qt6GuiPrivateConfig.cmake files; the COMPONENTS dispatch then fails
# even though the targets already exist. Mirror the guarded pattern from
# the project's top-level CMakeLists.txt so the tool builds on both
# Arch/openSUSE-style and Fedora-style Qt6 layouts.
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6GuiPrivate ${QT_MIN_VERSION} CONFIG REQUIRED)
endif()
if(NOT TARGET Qt6::QuickPrivate)
    find_package(Qt6QuickPrivate ${QT_MIN_VERSION} CONFIG REQUIRED)
endif()

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
    PhosphorRendering::PhosphorRendering
    PhosphorShaders::PhosphorShaders
    PhosphorWayland::PhosphorWayland
    PhosphorAudio::PhosphorAudio
    # The daemon's zone entry scaffold (zoneEntryPrologue / zoneEntryCandidates)
    # lives here. The tool installs the same scaffold so its shader assembly
    # matches the runtime byte-for-byte (see renderer.cpp).
    plasmazones_rendering
)

# Needed for the daemon's zoneentryscaffold.h, so a pack defining
# pZone/pImage is assembled here exactly as the daemon assembles it.
# ZoneUniformExtension no longer needs this path; it comes from the
# linked PhosphorRendering target.
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
    # WORKING_DIRECTORY pinned to the source tree so tests that read
    # sample JSON / fixtures via relative paths don't depend on where
    # ctest is invoked.
    add_test(
        NAME test_shader_render_layoutloader
        COMMAND test_shader_render_layoutloader
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(test_shader_render_metadataloader
        tests/test_metadataloader.cpp
        metadataloader.cpp
        metadataloader.h)
    set_target_properties(test_shader_render_metadataloader PROPERTIES AUTOMOC ON)
    # PhosphorShaders: metadataloader.cpp delegates parameter slot
    # resolution to the registry's own parsePackMetadata so its lane
    # numbering can't drift from the p_<id> preamble (T1.1).
    target_link_libraries(test_shader_render_metadataloader PRIVATE
        Qt6::Test Qt6::Core Qt6::Gui PhosphorShaders::PhosphorShaders)
    target_compile_features(test_shader_render_metadataloader PRIVATE cxx_std_20)
    add_test(
        NAME test_shader_render_metadataloader
        COMMAND test_shader_render_metadataloader
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # These two targets live outside tests/unit, so the TEST_LAUNCHER loop
    # there (which iterates only its own directory's tests) does not reach
    # them and they would run on the ambient session bus. Neither links
    # Qt6::DBus today, so nothing can hang right now, but the repo's rule is
    # that a test gets the isolated bus, and the next test added beside these
    # would silently opt out of it. Same launcher and same config file as
    # tests/unit/CMakeLists.txt, with the same graceful skip when
    # dbus-run-session or CMake 3.29 is unavailable.
    # Redirect XDG to the same throwaway tree tests/unit uses. This half matters
    # more than the bus half: without it a test that touches QStandardPaths
    # writes into the developer's real ~/.config/plasmazones. Neither loader
    # does today, which is exactly the argument that was accepted for the bus
    # isolation below, so apply it here for the same reason.
    set(_pz_sr_test_home "${CMAKE_BINARY_DIR}/test-xdg")
    # Created here as well as in tests/unit: relying on that directory to make
    # them first is an invisible one-way dependency between two sibling trees.
    file(MAKE_DIRECTORY
        "${_pz_sr_test_home}/config"
        "${_pz_sr_test_home}/data"
        "${_pz_sr_test_home}/state"
        "${_pz_sr_test_home}/cache")
    set_tests_properties(test_shader_render_layoutloader test_shader_render_metadataloader PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${_pz_sr_test_home}/config;XDG_DATA_HOME=${_pz_sr_test_home}/data;XDG_STATE_HOME=${_pz_sr_test_home}/state;XDG_CACHE_HOME=${_pz_sr_test_home}/cache")

    # Reuses tests/unit's cache entry rather than caching the same program
    # under a second name a user could set inconsistently.
    # Fail at configure time rather than as a confusing launcher error inside
    # ctest if the shared bus config ever moves.
    set(_pz_sr_bus_conf "${CMAKE_SOURCE_DIR}/tests/unit/test-session-bus.conf")
    if(NOT EXISTS "${_pz_sr_bus_conf}")
        message(FATAL_ERROR "shader-render tests: missing D-Bus isolation config at ${_pz_sr_bus_conf}")
    endif()
    find_program(_pz_dbus_run_session dbus-run-session)
    if(_pz_dbus_run_session AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        foreach(_pz_sr_test test_shader_render_layoutloader test_shader_render_metadataloader)
            set_target_properties(${_pz_sr_test} PROPERTIES
                TEST_LAUNCHER
                "${_pz_dbus_run_session};--config-file=${_pz_sr_bus_conf};--")
        endforeach()
    else()
        message(STATUS "shader-render tests: D-Bus isolation unavailable (needs dbus-run-session + CMake 3.29); "
                       "running on the ambient session bus")
    endif()
endif()
