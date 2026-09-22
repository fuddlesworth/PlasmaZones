// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. Phosphor.Power is a pure-QML
// module (PowerMenu + PowerTile), but qt_add_qml_module needs at least one
// C++ source so the generated plugin and type registrar have a compilation
// unit. The session actions the menu invokes come from SessionHost, which
// the shell process registers imperatively at runtime, so this module owns
// no C++ of its own. Any future C++ power primitive or foreign-type
// re-export goes here.
