// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. PopoutController carries
// the QML_ELEMENT macro directly on the class declaration, so the
// registration happens entirely through AUTOMOC. This file is the
// conventional "plugin main" entry point and exists so the module
// always has at least one C++ source even when the public surface
// shrinks. Adding a foreign-type re-export or a future C++ singleton
// goes here.
