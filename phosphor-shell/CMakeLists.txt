# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The Phosphor shell: the phosphor-shell binary, the phosphorctl IPC CLI,
# the bundled shell QML tree and the shell unit tests. Entered from the
# root CMakeLists only when BUILD_PHOSPHOR_SHELL is ON.
#
# Known tier inversion: the binary links plasmazones_rendering and
# plasmazones_shared_qml (ShellChrome hosts SurfaceDecoration.qml and the
# SurfaceShaderItem stage), and plasmazones_rendering carries
# plasmazones_core with it. Until those two move into a phosphor library,
# this tier depends on the plasmazones tier.

add_subdirectory(cli/phosphorctl)       # phosphorctl call / list / schema / subscribe

# Build the bundled example shell as a Qt QML module so qmlcachegen
# and qmlsc run at build time. The resulting static module gets linked
# into the phosphor-shell binary below so the
# executable has a working default that doesn't depend on filesystem
# install (qrc fallback in ShellLoader::resolve()).
add_subdirectory(shell)

# Install the example phosphor-shell QML/shaders so users can copy
# them into their config dir and edit. Same source as the qrc above.
# The qrc is the build-time frozen copy used as final fallback. This
# install is the user-editable copy. ShellLoader::resolve() probes
# GenericConfigLocation (~/.config), then GenericDataLocation (this
# install), then qrc:/ (linked-in fallback) in that order.
install(DIRECTORY shell/
    DESTINATION ${KDE_INSTALL_DATADIR}/phosphor-shell
    FILES_MATCHING
        PATTERN "*.qml"
        PATTERN "*.frag"
        PATTERN "*.glsl"
)

add_executable(phosphor-shell
    src/main.cpp
    # The bar's IBarWidgetFactory registry owner + the generic
    # QML-component factory. First production owner of a UI-seam
    # registry; the Phosphor.Bar QML module stays registry-agnostic
    # (Phase 4.1).
    src/BarController.h
    src/barcontroller.cpp
    src/LayerPopoutTransport.h
    src/layerpopouttransport.cpp
    src/QmlComponentBarWidgetFactory.h
    src/qmlcomponentbarwidgetfactory.cpp
    src/ControlCenterController.h
    src/controlcentercontroller.cpp
    src/QmlComponentTileFactory.h
    src/qmlcomponenttilefactory.cpp
    # The popout transport stack: one router in front of the layer
    # transport and the bar-socket transport, so PopoutController
    # arbitrates both presentation paths.
    src/RoutingPopoutTransport.h
    src/routingpopouttransport.cpp
    src/SocketPopoutTransport.h
    src/socketpopouttransport.cpp
    # The engine-placed pane (A2 §4): a real toplevel the daemon
    # places, with the bar-socket transport as its floating fallback;
    # the bundled window rule that names its placement; and the
    # compositor blur request behind the bar's band.
    src/PanePopoutTransport.h
    src/panepopouttransport.cpp
    src/PaneRules.h
    src/panerules.cpp
    src/ShellEffects.h
    src/shelleffects.cpp
    src/ShellMotion.h
    src/shellmotion.cpp
    src/ShellGestures.h
    src/shellgestures.cpp
    src/ShellChrome.h
    src/shellchrome.cpp
    # The launcher's registry owner: built-in providers, the model the
    # launcher popout binds, and the clipboard + Toplevels services the
    # providers read that nothing else in the shell owns yet.
    src/LauncherController.h
    src/launchercontroller.cpp
    # The OSD registry owner + its generic QML-component factory, and
    # the toast broker: the per-screen OSDHost / ToastHost surfaces in
    # shell.qml read them as the OsdRegistry / ToastRegistry context
    # properties, and the `osd` / `notify` IpcTargets fan out through
    # them.
    src/OsdController.h
    src/osdcontroller.cpp
    src/QmlComponentOSDFactory.h
    src/qmlcomponentosdfactory.cpp
    src/ToastController.h
    src/toastcontroller.cpp
    # The PolicyKit agent owner (registers the shell as the session's
    # agent, starts each PAM conversation, carries the requester's pid
    # and the prompt's placement) and the picker strip's open state:
    # the PolkitRegistry / PickerRegistry context properties.
    src/PolkitController.h
    src/polkitcontroller.cpp
    src/PickerController.h
    src/pickercontroller.cpp
)

target_link_libraries(phosphor-shell
    PRIVATE
        PhosphorShell::PhosphorShell
        # Registry core (Registry<IBarWidgetFactory> + RegistryNotifier)
        # for BarController, and the Phosphor.Bar QML module + its plugin
        # registrar so the bar delegates resolve at runtime.
        PhosphorRegistry::PhosphorRegistry
        PhosphorShellBar::PhosphorShellBarQml
        PhosphorShellBarQmlplugin
        PhosphorShellPower::PhosphorShellPowerQml
        PhosphorShellPowerQmlplugin
        # Phosphor.ControlCenter + its registrar, so shell.qml's
        # control-center popout resolves the surface and its tiles.
        # The tiles import Phosphor.Service.* modules, which the
        # registerQmlTypes() block in shell/main.cpp already publishes.
        PhosphorShellControlCenter::PhosphorShellControlCenterQml
        PhosphorShellControlCenterQmlplugin
        # The launcher: its core (LauncherModel + the built-in providers
        # LauncherController materialises), the Phosphor.Launcher module
        # + registrar for shell.qml's launcher popout. The clipboard
        # service the controller owns for the clipboard provider is
        # already listed with the other services below.
        PhosphorShellLauncher::PhosphorShellLauncher
        PhosphorShellLauncher::PhosphorShellLauncherQml
        PhosphorShellLauncherQmlplugin
        # Phosphor.OSD and Phosphor.Notifications + their registrars,
        # for the per-screen OSD and toast overlays shell.qml mounts.
        PhosphorShellOsd::PhosphorShellOsdQml
        PhosphorShellOsdQmlplugin
        PhosphorShellNotifications::PhosphorShellNotificationsQml
        PhosphorShellNotificationsQmlplugin
        # Phosphor.Dashboard + its registrar: the dashboard and the
        # cheatsheet popouts shell.qml opens, and the ShortcutCatalog
        # the sheet reads its chords through.
        PhosphorShellDashboard::PhosphorShellDashboardQml
        PhosphorShellDashboardQmlplugin
        # Phosphor.Lock + its registrar, for the per-screen lock
        # surfaces shell.qml mounts (LockSurface windows + LockScreen).
        PhosphorShellLock::PhosphorShellLockQml
        PhosphorShellLockQmlplugin
        # Phosphor.Picker (the wallpaper and theme strip, whose C++
        # types ride in the module) and Phosphor.Polkit (the prompt)
        # + their registrars, for the per-screen strips and the polkit
        # popout shell.qml mounts. The picker's committed palette is
        # loaded at startup through PaletteStore (see main.cpp).
        PhosphorShellPicker::PhosphorShellPickerQml
        PhosphorShellPickerQmlplugin
        # ShellGestures subscribes to the daemon's gestureReported relay
        # by the protocol's interface constants.
        PhosphorProtocol::PhosphorProtocol
        # ShellChrome: the decoration tree + pack registry (PhosphorSurface), the
        # preset store it flattens through (PhosphorShaders — declared rather than
        # taken transitively, because ShellChrome.h includes ShaderPresetStore.h and
        # holds one by unique_ptr), the SurfaceShaderItem stage
        # (plasmazones_rendering) and the shared SurfaceDecoration.qml host
        # (org.plasmazones.common) that draws a pack chain on the chrome.
        PhosphorShaders::PhosphorShaders
        PhosphorSurface::PhosphorSurface
        plasmazones_rendering
        plasmazones_shared_qml
        plasmazones_shared_qmlplugin
        # ShellMotion owns the profile registry behind SettleAnimation
        # and publishes the QML-side motion defaults.
        PhosphorAnimation::PhosphorAnimation
        PhosphorAnimation::PhosphorAnimationQml
        PhosphorAnimationQmlplugin
        PhosphorShellPolkit::PhosphorShellPolkitQml
        PhosphorShellPolkitQmlplugin
        PhosphorTheme::PhosphorTheme
        # IpcRouter + the Phosphor.Ipc module that publishes IpcTarget.
        # The router has to be installed on each engine (see main.cpp)
        # or every IpcTarget logs a warning and stays inert.
        PhosphorIpc::PhosphorIpc
        PhosphorIpc::PhosphorIpcQml
        PhosphorIpcQmlplugin
        # Popout policy + the QML host it drives. Static QML modules ship
        # both a library and a plugin registrar; the registrar is what
        # publishes the module to a QQmlEngine, so without it the
        # transport's PopoutHost.qml load would fail at runtime.
        PhosphorPopout::PhosphorPopout
        PhosphorPopout::PhosphorPopoutQml
        PhosphorPopoutQmlplugin
        PhosphorServiceIconTheme::PhosphorServiceIconTheme
        PhosphorServiceSni::PhosphorServiceSni
        PhosphorServiceUPower::PhosphorServiceUPower
        PhosphorServiceMpris::PhosphorServiceMpris
        PhosphorServicePipeWire::PhosphorServicePipeWire
        PhosphorServiceNetwork::PhosphorServiceNetwork
        PhosphorServiceBluetooth::PhosphorServiceBluetooth
        PhosphorServiceBrightness::PhosphorServiceBrightness
        PhosphorServiceNotifications::PhosphorServiceNotifications
        PhosphorServicePolkit::PhosphorServicePolkit
        PhosphorServiceIdle::PhosphorServiceIdle
        PhosphorServiceClipboard::PhosphorServiceClipboard
        PhosphorServiceLock::PhosphorServiceLock
        PhosphorServiceSession::PhosphorServiceSession
        PhosphorLayer::PhosphorLayer
        PhosphorWayland::PhosphorWayland
        PhosphorRendering::PhosphorRendering
        # Linked from shell/ (the bundled shell QML module): bakes the default shell.qml
        # tree into the binary as a qt-qml-module resource. Two effects:
        # (1) qmlcachegen / qmlsc run at build time (faster cold start,
        # typed bindings compile to C++); (2) when no user config exists
        # under XDG paths, ShellLoader falls back to qrc:/qt/qml/...
        # so the binary works without the optional DATADIR install.
        phosphor_shell_exampleplugin
        # The pane rule is built with the rules library's own types
        # and seeded over org.plasmazones.Rules.
        PhosphorRules::PhosphorRules
        PhosphorProtocol::PhosphorProtocol
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::DBus
)

# Compositor blur behind the bar's band goes through the kde-blur
# Wayland protocol, which KWindowEffects speaks. Optional: without it
# the band is a plain navy tint and ShellEffects.blurAvailable() says
# so, rather than the whole shell tier failing to configure.
find_package(KF6WindowSystem QUIET)
if(KF6WindowSystem_FOUND)
    target_link_libraries(phosphor-shell PRIVATE KF6::WindowSystem)
    target_compile_definitions(phosphor-shell PRIVATE PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM)
else()
    message(STATUS "KF6WindowSystem not found: the Phosphor bar band gets no compositor blur")
endif()

install(TARGETS phosphor-shell DESTINATION ${KDE_INSTALL_BINDIR})

install(FILES icons/hicolor/scalable/apps/phosphor-shell.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
)
install(FILES icons/hicolor-light/scalable/apps/phosphor-shell.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor-light/scalable/apps
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Aggregate target for the moon workspace: `cmake --build build --target phosphor-shell-tier`.
include(PhosphorTier)
phosphor_tier_target(phosphor-shell-tier)
