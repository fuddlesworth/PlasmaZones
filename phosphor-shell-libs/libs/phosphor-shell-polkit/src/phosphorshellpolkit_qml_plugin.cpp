// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. Phosphor.Polkit is a
// pure-QML module (PolkitPrompt, PolkitAnchor, PolkitDim, PolkitAction)
// over the phosphor-service-polkit agent, which it reaches by property
// and method name rather than by type; nothing is registered here.
// qt_add_qml_module needs at least one C++ source so the generated
// plugin and type registrar have a compilation unit.
