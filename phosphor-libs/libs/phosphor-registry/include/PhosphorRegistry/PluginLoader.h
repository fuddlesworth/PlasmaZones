// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/PluginCatalog.h>
#include <PhosphorRegistry/Registry.h>
#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>

namespace PhosphorRegistry {

/// Loads every bar-widget plugin into a Registry<IBarWidgetFactory> and keeps
/// it in step with the plugins on disk.
///
/// Discovery, metadata checks, overrides and hot reload come from a
/// PluginCatalog over the `bar-widget` seam (see barWidgetSeam). A plugin's
/// root object implements IBarWidgetPlugin. Unlike Facet modules, which load
/// lazily, every bar-widget plugin is loaded as soon as it is found, because
/// the bar needs every factory to lay itself out.
///
/// A plugin that disappears from disk is unregistered and pluginUnloaded is
/// emitted. Its library stays mapped (PluginCatalog never unloads), so widgets
/// it already created keep working until their parents delete them.
///
/// The Registry must outlive the loader. GUI thread only.
class PHOSPHORREGISTRY_EXPORT PluginLoader : public QObject
{
    Q_OBJECT
public:
    /// The bar-widget seam: plugins in `<root>/bar-widget/`, IID
    /// PhosphorRegistryBarWidgetPlugin_iid, ApiVersion 1.
    [[nodiscard]] static PluginSeam barWidgetSeam();

    /// @p searchPaths highest priority first. Empty means
    /// PluginCatalog::defaultSearchPaths for the bar-widget seam.
    explicit PluginLoader(Registry<IBarWidgetFactory>* registry,
                          const QList<PluginSearchPath>& searchPaths = QList<PluginSearchPath>(),
                          QObject* parent = nullptr);
    ~PluginLoader() override;
    Q_DISABLE_COPY_MOVE(PluginLoader)

    /// The first scan: find, load and register every plugin, and arm the
    /// watch. A second call rescans.
    void scanAndLoad();
    /// Rescan now instead of waiting for the watch.
    void rescanNow();

    /// The ids of the plugins currently registered, sorted.
    [[nodiscard]] QStringList loadedPluginIds() const;

    [[nodiscard]] PluginCatalog& catalog();
    [[nodiscard]] const PluginCatalog& catalog() const;

Q_SIGNALS:
    /// After every scan, whether or not anything changed.
    void rescanCompleted();
    /// A plugin's factory was registered.
    void pluginLoaded(const QString& id);
    /// A plugin's factory was unregistered because its file is gone.
    void pluginUnloaded(const QString& id);

private:
    void loadPlugin(const QString& id);
    void unloadPlugin(const QString& id);

    /// Non-owning, never null after construction.
    Registry<IBarWidgetFactory>* m_registry = nullptr;
    PluginCatalog m_catalog;
    /// The factories this loader registered, by plugin id.
    QHash<QString, std::shared_ptr<IBarWidgetFactory>> m_factories;
};

} // namespace PhosphorRegistry
