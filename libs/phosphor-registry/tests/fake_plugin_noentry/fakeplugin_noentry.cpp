// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Test fixture for PluginLoader's missing-entry-point path. This .so
// builds successfully but does NOT export the canonical
// phosphor_registry_create_factory symbol — only an unrelated stub
// so the .so isn't empty (some linkers refuse to produce a module
// library with no public symbols). The loader must log a warning
// ("missing entry point") and unload the .so cleanly.

#include <QtCore/qglobal.h>

extern "C" Q_DECL_EXPORT int phosphor_registry_noentry_marker()
{
    return 0;
}
