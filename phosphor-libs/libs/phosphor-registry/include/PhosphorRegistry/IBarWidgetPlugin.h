// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QtPlugin>

#include <memory>

namespace PhosphorRegistry {

/// The root object of a bar-widget plugin.
///
/// A bar-widget plugin is a Qt plugin in the `bar-widget` seam (see
/// PluginLoader::barWidgetSeam): its root QObject implements this interface,
/// declares `Q_PLUGIN_METADATA(IID PhosphorRegistryBarWidgetPlugin_iid FILE
/// "metadata.json")`, and its metadata carries `"Id"` and `"ApiVersion"`.
///
/// Both binaries must use the same C++ standard library, because the factory
/// is created in the plugin and destroyed by the loader.
class IBarWidgetPlugin
{
public:
    virtual ~IBarWidgetPlugin() = default;

    /// A fresh factory whose id() equals the plugin's metadata `"Id"`, or
    /// nullptr when it cannot be built. Called once per plugin load. Must not
    /// throw.
    [[nodiscard]] virtual std::unique_ptr<IBarWidgetFactory> createFactory() = 0;
};

} // namespace PhosphorRegistry

#define PhosphorRegistryBarWidgetPlugin_iid "org.phosphor_works.Lambent.BarWidget/1"
Q_DECLARE_INTERFACE(PhosphorRegistry::IBarWidgetPlugin, PhosphorRegistryBarWidgetPlugin_iid)
