// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. Phosphor.Launcher is a
// pure-QML module (Launcher + LauncherResultRow) over the C++ core; the
// core's LauncherModel reaches QML as a context property or a plain
// QObject, not as a registered type, so nothing is registered here.
// qt_add_qml_module needs at least one C++ source so the generated
// plugin and type registrar have a compilation unit.
