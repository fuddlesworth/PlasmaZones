# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Phosphor shell unit tests. These compile the shell sources they need
# directly rather than linking the phosphor-shell binary. The tier is only
# entered with BUILD_PHOSPHOR_SHELL=ON, so every shell-tier library target
# exists here; the if(TARGET ...) guards are kept so a test still reads as
# declaring what it links.

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

include_directories(
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src
    ${CMAKE_SOURCE_DIR}/plasmazones/src
    ${CMAKE_BINARY_DIR}/plasmazones/src  # generated export headers
    ${CMAKE_SOURCE_DIR}/plasmazones/tests/unit  # shared fixtures as "helpers/X.h"
)

# ═══════════════════════════════════════════════════════════════════════════════
# Shell-process controllers, factories and popout transports
# ═══════════════════════════════════════════════════════════════════════════════
# These targets compile the shell sources they need directly rather than
# linking the phosphor-shell binary, so a guard test stays a unit test of
# one class. Before the tier split they lived in the plasmazones test tree
# and were deliberately ungated so the default (shell-off) configure still
# built them; now the whole tier is entered only with BUILD_PHOSPHOR_SHELL
# on, and the build-shell CI job is what runs them.
#
# The classes live in the GPL shell binary, so they are tested here rather
# than from the LGPL bar module's own QuickTest suite (which covers the QML
# delegates). The headers are listed so AUTOMOC generates BarController's
# moc: the Q_OBJECT vtable and factoryIdsChanged live there, not in the .cpp.
add_executable(test_bar_controller
    test_bar_controller.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/BarController.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/barcontroller.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/QmlComponentBarWidgetFactory.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/qmlcomponentbarwidgetfactory.cpp
)
target_link_libraries(test_bar_controller
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        PhosphorRegistry::PhosphorRegistry
)
# src-rooted includes, matching the other tests that compile src/ sources,
# so the test survives a move of either tree.
target_include_directories(test_bar_controller PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
add_test(NAME test_bar_controller COMMAND test_bar_controller)

# The OSD registry owner, same shape and same reasoning as test_bar_controller.
add_executable(test_osd_controller
    test_osd_controller.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/OsdController.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/osdcontroller.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/QmlComponentOSDFactory.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/qmlcomponentosdfactory.cpp
)
target_link_libraries(test_osd_controller
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        PhosphorRegistry::PhosphorRegistry
)
target_include_directories(test_osd_controller PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
add_test(NAME test_osd_controller COMMAND test_osd_controller)

# The shell's motion root: settle profile, QML default publication, and
# the portal's reduced-motion decoding.
add_executable(test_shell_motion
    test_shell_motion.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/ShellMotion.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/shellmotion.cpp
)
target_link_libraries(test_shell_motion
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        PhosphorAnimation::PhosphorAnimation
        PhosphorAnimation::PhosphorAnimationQml
)
target_include_directories(test_shell_motion PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
add_test(NAME test_shell_motion COMMAND test_shell_motion)

# The gesture relay: the daemon's gestureReported split into swiped /
# pinched over a fake daemon on the private bus.
add_executable(test_shell_gestures
    test_shell_gestures.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/ShellGestures.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/shellgestures.cpp
)
target_link_libraries(test_shell_gestures
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::DBus
        PhosphorProtocol::PhosphorProtocol
)
target_include_directories(test_shell_gestures PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
add_test(NAME test_shell_gestures COMMAND test_shell_gestures)

# The chrome decoration resolver: tree JSON + the bundled packs to a stage
# list per shell surface path.
#
# Gated on the theme target, unlike its siblings in this group. shellchrome.cpp
# includes PhosphorTheme/PaletteStore.h and connects paletteChanged, and
# libs/phosphor-theme is only added under BUILD_PHOSPHOR_SHELL, so this is the
# one file here whose dependencies are not all unconditional. Without the guard
# the default BUILD_TESTING=ON configure fails at generate and no test builds.
if(TARGET PhosphorTheme::PhosphorTheme)
    add_executable(test_shell_chrome
        test_shell_chrome.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/ShellChrome.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/shellchrome.cpp
    )
    target_link_libraries(test_shell_chrome
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::DBus
            Qt6::Gui
            PhosphorProtocol::PhosphorProtocol
            # Declared rather than taken transitively through PhosphorSurface:
            # ShellChrome.h includes <PhosphorShaders/ShaderPresetStore.h> and holds
            # the store by unique_ptr, so the edge belongs to this target too.
            PhosphorShaders::PhosphorShaders
            PhosphorSurface::PhosphorSurface
            PhosphorTheme::PhosphorTheme
    )
    target_compile_definitions(test_shell_chrome PRIVATE PZ_BUNDLED_SURFACE_DIR="${CMAKE_SOURCE_DIR}/plasmazones/data/surface")
    target_include_directories(test_shell_chrome PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
    add_test(NAME test_shell_chrome COMMAND test_shell_chrome)
endif()

# The toast broker behind the `notify` IpcTarget.
add_executable(test_toast_controller
    test_toast_controller.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/ToastController.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell/src/toastcontroller.cpp
)
target_link_libraries(test_toast_controller
    PRIVATE
        Qt6::Test
        Qt6::Core
)
target_include_directories(test_toast_controller PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
add_test(NAME test_toast_controller COMMAND test_toast_controller)

# Ties BarController's registered type names to the QML the Phosphor.Bar
# module actually ships. Gated on the bar module target, since it links that
# module and its plugin registrar to resolve the types.
if(TARGET PhosphorShellBar::PhosphorShellBarQml)
    add_executable(test_bar_widget_types
        test_bar_widget_types.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/BarController.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/barcontroller.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/QmlComponentBarWidgetFactory.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/qmlcomponentbarwidgetfactory.cpp
    )
    target_include_directories(test_bar_widget_types PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
    target_link_libraries(test_bar_widget_types
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            PhosphorRegistry::PhosphorRegistry
            # The module AND its plugin registrar: without the registrar the
            # static module registers no types and every lookup would "fail"
            # for the wrong reason.
            PhosphorShellBar::PhosphorShellBarQml
            PhosphorShellBarQmlplugin
    )
    add_test(NAME test_bar_widget_types COMMAND test_bar_widget_types)
endif()

# LayerPopoutTransport, the shell's real popout transport (Phase 4.6),
# driven headless through phosphor-layer's mock transport + screen provider.
# Gated on the popout QML module because building PopoutHost in-test needs
# the module and its plugin registrar (the same reason test_popout_qml_enums
# links them); Phosphor.Theme comes along for the Motion tokens PopoutHost
# animates with.
if(TARGET PhosphorPopout::PhosphorPopoutQml AND TARGET PhosphorLayer::PhosphorLayer)
    add_executable(test_layer_popout_transport
        test_layer_popout_transport.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/LayerPopoutTransport.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/layerpopouttransport.cpp
    )
    target_include_directories(test_layer_popout_transport
        PRIVATE
            ${CMAKE_SOURCE_DIR}/phosphor-shell/src
            # The reusable phosphor-layer test mocks (mocks/mocktransport.h,
            # mocks/mockscreenprovider.h).
            ${CMAKE_SOURCE_DIR}/phosphor/libs/phosphor-layer/tests
    )
    target_link_libraries(test_layer_popout_transport
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            PhosphorLayer::PhosphorLayer
            PhosphorPopout::PhosphorPopout
            PhosphorPopout::PhosphorPopoutQml
            PhosphorPopoutQmlplugin
            PhosphorThemeQml
            PhosphorThemeQmlplugin
    )
    add_test(NAME test_layer_popout_transport COMMAND test_layer_popout_transport)
endif()

# SystemUsage's /proc arithmetic. Gated on the shell tier because it links
# the PhosphorShell library, which only exists when that tier is built.
if(TARGET PhosphorShell::PhosphorShell)
    add_executable(test_system_usage test_system_usage.cpp)
    target_link_libraries(test_system_usage
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorShell::PhosphorShell
    )
    add_test(NAME test_system_usage COMMAND test_system_usage)
endif()

# RoutingPopoutTransport: one IPopoutTransport in front of the layer and
# bar-socket transports, routed by popout id. Pure C++ over two in-test
# fakes, so it needs only the popout contract library — no layer stack, no
# QML engine, no Wayland.
if(TARGET PhosphorPopout::PhosphorPopout)
    add_executable(test_routing_popout_transport
        test_routing_popout_transport.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/RoutingPopoutTransport.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/routingpopouttransport.cpp
    )
    target_include_directories(test_routing_popout_transport PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
    target_link_libraries(test_routing_popout_transport
        PRIVATE
            Qt6::Test
            Qt6::Core
            PhosphorPopout::PhosphorPopout
    )
    add_test(NAME test_routing_popout_transport COMMAND test_routing_popout_transport)
endif()

# PanePopoutTransport: the engine-placed pane (A2 §4), a real toplevel
# built from FloatingWindow with the pane app id, hosted in
# Phosphor.Popout's PaneHost. Needs the shell library for FloatingWindow,
# the popout QML module + plugin for PaneHost, and Theme + Widgets for the
# band and stroke PaneHost draws. The controller it drives brings the
# registry and idle service along, as test_control_center_controller does.
if(TARGET PhosphorShell::PhosphorShell AND TARGET PhosphorPopout::PhosphorPopoutQml
   AND TARGET PhosphorShellWidgets::PhosphorShellWidgetsQml AND TARGET PhosphorServiceIdle::PhosphorServiceIdle)
    add_executable(test_pane_popout_transport
        test_pane_popout_transport.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/PanePopoutTransport.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/panepopouttransport.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/ControlCenterController.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/controlcentercontroller.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/QmlComponentTileFactory.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/qmlcomponenttilefactory.cpp
        # The transport rewrites the pane rule's zone before an open.
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/PaneRules.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/panerules.cpp
    )
    target_include_directories(test_pane_popout_transport PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
    target_link_libraries(test_pane_popout_transport
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            PhosphorShell::PhosphorShell
            PhosphorPopout::PhosphorPopout
            PhosphorPopout::PhosphorPopoutQml
            PhosphorPopoutQmlplugin
            PhosphorShellWidgets::PhosphorShellWidgetsQml
            PhosphorShellWidgetsQmlplugin
            PhosphorThemeQml
            PhosphorThemeQmlplugin
            PhosphorRegistry::PhosphorRegistry
            PhosphorServiceIdle::PhosphorServiceIdle
            PhosphorRules::PhosphorRules
            PhosphorProtocol::PhosphorProtocol
    )
    add_test(NAME test_pane_popout_transport COMMAND test_pane_popout_transport)
endif()

# PaneRules: the shell's bundled window rule for the control-center pane.
# Validates against the real action registry, so it links phosphor-rules;
# the seeding path is D-Bus and not exercised here.
if(TARGET PhosphorRules::PhosphorRules AND TARGET PhosphorProtocol::PhosphorProtocol)
    add_executable(test_pane_rules
        test_pane_rules.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/PaneRules.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/panerules.cpp
    )
    target_include_directories(test_pane_rules PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
    target_link_libraries(test_pane_rules
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::DBus
            PhosphorRules::PhosphorRules
            PhosphorProtocol::PhosphorProtocol
    )
    add_test(NAME test_pane_rules COMMAND test_pane_rules)
endif()

# ControlCenterController: the shell's tile registry owner and the
# target-screen resolver behind the bar socket. Needs a QGuiApplication
# (a real QScreen to resolve) and the QML engine's ownership API, hence
# Gui + Qml; runs offscreen. Links the registry (the controller owns a
# Registry<IControlCenterTileFactory>) and the idle service (the controller
# hands an IdleService* to IdleTile's initial properties).
if(TARGET PhosphorRegistry::PhosphorRegistry AND TARGET PhosphorServiceIdle::PhosphorServiceIdle)
    add_executable(test_control_center_controller
        test_control_center_controller.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/ControlCenterController.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/controlcentercontroller.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/QmlComponentTileFactory.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/qmlcomponenttilefactory.cpp
    )
    target_include_directories(test_control_center_controller PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
    target_link_libraries(test_control_center_controller
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            PhosphorRegistry::PhosphorRegistry
            PhosphorServiceIdle::PhosphorServiceIdle
    )
    add_test(NAME test_control_center_controller COMMAND test_control_center_controller)

    # The tile factory on its own. Pure engine-testable logic that had no
    # test, including the JavaScriptOwnership hand-off the control-center
    # surface depends on to destroy the tiles it built.
    add_executable(test_qml_component_tile_factory
        test_qml_component_tile_factory.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/QmlComponentTileFactory.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/qmlcomponenttilefactory.cpp
    )
    target_include_directories(test_qml_component_tile_factory PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
    target_link_libraries(test_qml_component_tile_factory
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            PhosphorRegistry::PhosphorRegistry
    )
    add_test(NAME test_qml_component_tile_factory COMMAND test_qml_component_tile_factory)
endif()

# SocketPopoutTransport, the bar-pocket popout transport.
# Needs the popout library for IPopoutTransport, and the control-center
# controller because the transport is the sole writer of its openScreen.
if(TARGET PhosphorPopout::PhosphorPopout AND TARGET PhosphorRegistry::PhosphorRegistry
   AND TARGET PhosphorServiceIdle::PhosphorServiceIdle)
    add_executable(test_socket_popout_transport
        test_socket_popout_transport.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/SocketPopoutTransport.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/socketpopouttransport.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/ControlCenterController.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/controlcentercontroller.cpp
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/QmlComponentTileFactory.h
        ${CMAKE_SOURCE_DIR}/phosphor-shell/src/qmlcomponenttilefactory.cpp
    )
    target_include_directories(test_socket_popout_transport PRIVATE ${CMAKE_SOURCE_DIR}/phosphor-shell/src)
    target_link_libraries(test_socket_popout_transport
        PRIVATE
            Qt6::Test
            Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            PhosphorPopout::PhosphorPopout
            PhosphorRegistry::PhosphorRegistry
            PhosphorServiceIdle::PhosphorServiceIdle
    )
    add_test(NAME test_socket_popout_transport COMMAND test_socket_popout_transport)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Headless environment + config isolation
# Apply a shared environment to every test in this directory so new tests added
# above automatically inherit it - a hand-maintained list rotted and left tests
# aborting in CI when the author forgot to append.
#
#   QT_QPA_PLATFORM=offscreen — headless rendering.
#   XDG_{CONFIG,DATA,STATE,CACHE}_HOME — redirect QStandardPaths into a throwaway
#     build-tree directory so tests that exercise config-writing paths
#     (LayoutManager assignments, mode toggles, the Rule store) never
#     mutate the developer's real ~/.config/plasmazones. Without this, running
#     ctest wrote sample-screen rules (e.g. "eDP-1") into the live config.
#
# IMPORTANT: get_property(... PROPERTY TESTS) reads the test list at CMake
# configure time, which means tests added BELOW this line are NOT covered.
# Always add new test targets ABOVE this block — never after it.
# ═══════════════════════════════════════════════════════════════════════════════
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)

# The shared sweep. It supplies the private-bus TEST_LAUNCHER and a per-target
# XDG sandbox; the bus config declares no service directories, so the installed
# daemon cannot be activated onto the private bus and hold the test's stdout
# pipe open after the test itself has passed.
phosphor_apply_directory_test_isolation()

# ─── ABOVE THIS LINE: ADD NEW TESTS — BELOW THIS LINE: env wiring only ───

# Two outputs for the control-center controller, which the sweep above cannot
# express because it gives every test the same single-screen offscreen QPA.
# screenOf resolves an item's window screen and falls back to the primary, and
# with one output those are the same pointer, so the resolution branch could be
# deleted with the suite still green. Appended AFTER the sweep on purpose: the
# environment list is read last-wins, so this overrides the plain offscreen
# entry rather than fighting it.
if(TARGET test_control_center_controller)
    phosphor_append_test_environment(test_control_center_controller
        "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/two-screens.json")
endif()
