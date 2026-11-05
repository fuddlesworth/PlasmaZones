// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. Phosphor.Lock's one C++
// type (LockSurfaceWindow, published as `LockSurface`) is registered
// through QML_NAMED_ELEMENT on its declaration; the rest of the module is
// QML. qt_add_qml_module still wants a source of its own so the generated
// plugin and type registrar have a compilation unit.
