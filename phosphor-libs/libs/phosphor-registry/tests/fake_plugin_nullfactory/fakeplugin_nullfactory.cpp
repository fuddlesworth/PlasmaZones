// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Test fixture for PluginLoader's null-factory-return path. The
// canonical Phase-1.3 entry point is exported but returns nullptr
// rather than a real factory — simulates a plugin whose construction
// failed (e.g. a required service was unavailable at load time). The
// loader must log a warning and skip the plugin.

#include <PhosphorRegistry/IBarWidgetFactory.h>

extern "C" Q_DECL_EXPORT PhosphorRegistry::IBarWidgetFactory* phosphor_registry_create_factory()
{
    return nullptr;
}
