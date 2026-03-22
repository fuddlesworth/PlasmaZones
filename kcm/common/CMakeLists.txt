# PlasmaZones KCM common sources
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Qt-only utility classes shared between the About KCM and standalone settings app.
# Both compile these sources directly rather than linking a static library,
# to avoid pulling in unnecessary KDE Framework dependencies.
#
# Files here:
#   - DaemonController: D-Bus daemon lifecycle (start/stop/status)
#   - ScreenHelper: Connected monitor enumeration
#   - ScreenProvider: Abstract screen data provider
#   - DBusUtils (DaemonDBus): D-Bus call helper to daemon
#   - org_plasmazones_common/qmldir: QML module registration for ZonePreview/CategoryBadge
