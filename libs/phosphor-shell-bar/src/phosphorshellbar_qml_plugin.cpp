// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. Phosphor.Bar is a
// pure-QML module (BarHost + Slot + the built-in bar-widget delegates),
// but qt_add_qml_module needs at least one C++ source so the generated
// plugin and type registrar have a compilation unit. The C++ that owns
// the IBarWidgetFactory registry and instantiates the delegates
// (BarController + QmlComponentBarWidgetFactory) lives in the shell
// binary (src/shell), so the module stays registry-agnostic. Any future
// C++ bar primitive or foreign-type re-export goes here.
