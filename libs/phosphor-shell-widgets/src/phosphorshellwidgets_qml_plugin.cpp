// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. Phosphor.Widgets is a
// pure-QML module today (the atoms are .qml files with no backing C++
// types), but qt_add_qml_module still needs at least one C++ source so
// the generated plugin and type registrar have a compilation unit. This
// is the conventional "plugin main" entry point and the home for any
// future C++ atom (a custom-painted shape, a QQuickPaintedItem) or
// foreign-type re-export the module grows.
