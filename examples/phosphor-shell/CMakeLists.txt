# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bundle the example shell QML/shaders as a Qt QML module. Three things
# we get from doing it this way instead of plain install(DIRECTORY ...):
#
#   1. qmlcachegen runs at build time → cold-start parse cost amortised
#      into the binary, not paid by every user on every launch.
#   2. qmlsc type-checks bindings; typed bindings compile to C++ skipping
#      the JS engine on the hot binding path.
#   3. The resource is linked into the phosphor-shell binary, so a fresh
#      install with no XDG config and no /usr/share/phosphor-shell/ install
#      still has a working default shell to fall back on (ShellLoader's
#      resolve() picks up the qrc:/ url when filesystem search misses).
#
# The same files are still installed to ${DATADIR}/phosphor-shell/ via
# the root CMakeLists install(DIRECTORY) so users can `cp -r` and edit.
# Both paths read identical source — the resource is the build-time
# snapshot, the installed files are the user-editable copy.

cmake_minimum_required(VERSION 3.16)

find_package(Qt6 6.10 REQUIRED COMPONENTS Quick Qml)

# qt_add_qml_module's metatype generation walks AUTOMOC-produced
# json files. Without AUTOMOC the call fails inside qt6_extract_metatypes
# even for a pure-QML module that ships no C++ types — the macro emits
# the registration TU unconditionally.
set(CMAKE_AUTOMOC ON)

qt_add_qml_module(phosphor_shell_example
    URI Phosphor.Shell.Example
    VERSION 1.0
    STATIC
    RESOURCE_PREFIX /qt/qml
    # The module's build-tree output directory must END in the URI path
    # (Phosphor/Shell/Example) or qmllint and other QML tooling can't
    # resolve the module. The default is CMAKE_CURRENT_BINARY_DIR, which
    # doesn't — so place it under a URI-shaped subdirectory. This is a
    # build-tree path only; the linked-in resource still lives at
    # qrc:/qt/qml/Phosphor/Shell/Example via RESOURCE_PREFIX above.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Phosphor/Shell/Example
    QML_FILES
        shell.qml
        TopPanel.qml
        Taskbar.qml
        PanelPopupHost.qml
        PanelPopup.qml
        SettingsWindow.qml
        CalendarContent.qml
        MenuContent.qml
        MprisContent.qml
        MprisPlayerState.qml
        MprisWidget.qml
        TrayMenuPopup.qml
    RESOURCES
        shaders/frosted_glass.frag
        shaders/gradient.frag
        shaders/corners.glsl
        shaders/shadow.glsl
)
