// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/PluginLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QStringList>

#include <memory>

namespace PhosphorRegistryPluginDemo {

// Mirrors the in-process demo's controller but adds a PluginLoader
// pointed at a configurable plugin root. The same Registry receives
// both built-in factories (registered explicitly) and plugin-loaded
// factories (registered by the loader on each rescan).
class DemoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList factoryIds READ factoryIds NOTIFY factoryIdsChanged)
    Q_PROPERTY(QString pluginRoot READ pluginRoot CONSTANT)
public:
    DemoController(QQmlEngine* engine, QString pluginRoot, QObject* parent = nullptr);
    ~DemoController() override;

    // Bar QML invokes this from rebuildBar() for each id in
    // factoryIds.
    Q_INVOKABLE QQuickItem* createWidgetFor(const QString& id, QQuickItem* parent);

    // QML "Reload" button hook. Forces a synchronous rescan of the
    // plugin root — useful when iterating on a plugin without
    // waiting for the watcher's debounce.
    Q_INVOKABLE void reloadPlugins();

    [[nodiscard]] QStringList factoryIds() const;
    [[nodiscard]] QString pluginRoot() const;

Q_SIGNALS:
    void factoryIdsChanged();

private:
    void registerBuiltins();

    QPointer<QQmlEngine> m_engine;
    QString m_pluginRoot;
    std::unique_ptr<PhosphorRegistry::Registry<PhosphorRegistry::IBarWidgetFactory>> m_registry;
    std::unique_ptr<PhosphorRegistry::PluginLoader> m_loader;
};

} // namespace PhosphorRegistryPluginDemo
