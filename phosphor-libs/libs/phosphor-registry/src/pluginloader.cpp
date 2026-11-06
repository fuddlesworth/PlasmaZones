// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRegistry/PluginLoader.h>

#include <PhosphorRegistry/IBarWidgetPlugin.h>

#include <QLoggingCategory>

#include <algorithm>

namespace PhosphorRegistry {

namespace {
Q_LOGGING_CATEGORY(lcPluginLoader, "phosphorregistry.pluginloader")
} // namespace

PluginSeam PluginLoader::barWidgetSeam()
{
    return PluginSeam{QStringLiteral("bar-widget"), QStringLiteral(PhosphorRegistryBarWidgetPlugin_iid), 1};
}

PluginLoader::PluginLoader(Registry<IBarWidgetFactory>* registry, const QList<PluginSearchPath>& searchPaths,
                           QObject* parent)
    : QObject(parent)
    , m_registry(registry)
    , m_catalog(barWidgetSeam())
{
    if (!m_registry) {
        qFatal("PluginLoader: registry must not be null");
    }
    if (!searchPaths.isEmpty()) {
        m_catalog.setSearchPaths(searchPaths);
    }
    connect(&m_catalog, &PluginCatalog::pluginAdded, this, &PluginLoader::loadPlugin);
    connect(&m_catalog, &PluginCatalog::pluginRemoved, this, &PluginLoader::unloadPlugin);
    connect(&m_catalog, &PluginCatalog::scanCompleted, this, &PluginLoader::rescanCompleted);
}

PluginLoader::~PluginLoader()
{
    // Take this loader's factories out of the registry, which outlives it.
    // The libraries stay mapped (PluginCatalog never unloads), so a factory
    // the registry's other users still hold remains valid.
    const QStringList ids = m_factories.keys();
    for (const QString& id : ids) {
        m_registry->unregisterFactory(id);
    }
}

void PluginLoader::scanAndLoad()
{
    m_catalog.scan();
}

void PluginLoader::rescanNow()
{
    m_catalog.scan();
}

QStringList PluginLoader::loadedPluginIds() const
{
    QStringList ids = m_factories.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

PluginCatalog& PluginLoader::catalog()
{
    return m_catalog;
}

const PluginCatalog& PluginLoader::catalog() const
{
    return m_catalog;
}

void PluginLoader::loadPlugin(const QString& id)
{
    if (m_factories.contains(id)) {
        return;
    }
    QString error;
    QObject* const root = m_catalog.load(id, &error);
    if (!root) {
        qCWarning(lcPluginLoader).noquote() << "PluginLoader: bar-widget plugin" << id << "did not load:" << error;
        return;
    }
    auto* const plugin = qobject_cast<IBarWidgetPlugin*>(root);
    if (!plugin) {
        qCWarning(lcPluginLoader).noquote()
            << "PluginLoader: bar-widget plugin" << id << "has a root object that does not implement IBarWidgetPlugin";
        return;
    }
    std::shared_ptr<IBarWidgetFactory> factory = plugin->createFactory();
    if (!factory) {
        qCWarning(lcPluginLoader).noquote() << "PluginLoader: bar-widget plugin" << id << "returned no factory";
        return;
    }
    if (factory->id() != id) {
        qCWarning(lcPluginLoader).noquote() << "PluginLoader: bar-widget plugin" << id << "returned a factory with id"
                                            << factory->id() << "- the ids must match";
        return;
    }
    if (!m_registry->registerFactory(factory)) {
        qCWarning(lcPluginLoader).noquote()
            << "PluginLoader: bar-widget plugin" << id << "was not registered; the registry already has that id";
        return;
    }
    m_factories.insert(id, std::move(factory));
    Q_EMIT pluginLoaded(id);
}

void PluginLoader::unloadPlugin(const QString& id)
{
    if (m_factories.remove(id) == 0) {
        return;
    }
    m_registry->unregisterFactory(id);
    Q_EMIT pluginUnloaded(id);
}

} // namespace PhosphorRegistry
