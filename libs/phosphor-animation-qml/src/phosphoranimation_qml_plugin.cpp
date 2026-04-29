// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Glue translation unit for qt_add_qml_module. The Q_GADGET types in
// include/PhosphorAnimationQml/ are header-inline; the QML type
// registration machinery needs at least one .cpp in the SOURCES list
// to have an AUTOMOC-scanned compilation target. Non-header sources
// (phosphorcurve.cpp, qtquickclockmanager.cpp) already satisfy that,
// but keeping this file around as the conventional "plugin main"
// entry point makes future additions (Q_OBJECT singletons, foreign
// type re-exports) easy to land without restructuring the module's
// SOURCES list.
