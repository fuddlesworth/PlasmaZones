// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. Phosphor.Dashboard is QML
// (Dashboard, Cheatsheet, FullMap and their cells) plus the one C++ type
// the sheet reads its chords through (ShortcutCatalog, registered by its
// own QML_ELEMENT); qt_add_qml_module needs a compilation unit for the
// generated plugin and type registrar regardless, and this is it.
