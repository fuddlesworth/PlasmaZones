// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. Phosphor.ControlCenter is
// a pure-QML module (ControlCenter + Tile + DetailPanel), but
// qt_add_qml_module needs at least one C++ source so the generated plugin
// and type registrar have a compilation unit. Any future C++ control-center
// primitive or foreign-type re-export goes here.
