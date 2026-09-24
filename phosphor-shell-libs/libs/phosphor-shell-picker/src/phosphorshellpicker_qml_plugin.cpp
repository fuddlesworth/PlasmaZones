// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. Phosphor.Picker's C++
// types (WallpaperCandidates, RetintController, ThemePresets) carry
// QML_ELEMENT and are compiled into the module directly, so the type
// registrar finds them through their headers; nothing is registered by
// hand here. qt_add_qml_module still wants a plain source for the
// generated plugin to compile against, which this is.
