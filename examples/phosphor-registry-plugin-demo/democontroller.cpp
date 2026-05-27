// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoController.h"

#include "QmlComponentBarWidgetFactory.h"

#include <PhosphorRegistry/RegistryNotifier.h>

#include <QQmlEngine>
#include <QUrl>

using namespace PhosphorRegistry;

namespace PhosphorRegistryPluginDemo {

DemoController::DemoController(QQmlEngine* engine, QString pluginRoot, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
    , m_pluginRoot(std::move(pluginRoot))
    , m_registry(std::make_unique<Registry<IBarWidgetFactory>>())
{
    QObject::connect(m_registry->notifier(), &RegistryNotifier::factoryRegistered, this,
                     &DemoController::factoryIdsChanged);
    QObject::connect(m_registry->notifier(), &RegistryNotifier::factoryUnregistered, this,
                     &DemoController::factoryIdsChanged);

    registerBuiltins();

    m_loader = std::make_unique<PluginLoader>(m_registry.get(), m_pluginRoot);
    m_loader->scanAndLoad();
    // Synchronous initial rescan so the bar's first paint has the
    // plugin set already populated. Without this the bar would
    // start with only the built-ins and the plugin would pop in
    // ~50 ms later via the watcher's debounced first event.
    m_loader->rescanNow();
}

DemoController::~DemoController() = default;

void DemoController::registerBuiltins()
{
    m_registry->registerFactory(std::make_shared<QmlComponentBarWidgetFactory>(
        QStringLiteral("clock"), QStringLiteral("Clock"),
        QUrl(QStringLiteral("qrc:/qt/qml/Phosphor/RegistryPluginDemo/ClockWidget.qml")),
        QStringList{QStringLiteral("bar.widget")}));
    m_registry->registerFactory(std::make_shared<QmlComponentBarWidgetFactory>(
        QStringLiteral("colorsquare"), QStringLiteral("Color Square"),
        QUrl(QStringLiteral("qrc:/qt/qml/Phosphor/RegistryPluginDemo/ColorSquareWidget.qml")),
        QStringList{QStringLiteral("bar.widget")}));
}

QQuickItem* DemoController::createWidgetFor(const QString& id, QQuickItem* parent)
{
    auto factory = m_registry->factory(id);
    if (!factory) {
        return nullptr;
    }
    return factory->createWidget(m_engine.data(), parent);
}

void DemoController::reloadPlugins()
{
    if (m_loader) {
        m_loader->rescanNow();
    }
}

QStringList DemoController::factoryIds() const
{
    QStringList ids = m_registry->ids();
    ids.sort();
    return ids;
}

QString DemoController::pluginRoot() const
{
    return m_pluginRoot;
}

} // namespace PhosphorRegistryPluginDemo
