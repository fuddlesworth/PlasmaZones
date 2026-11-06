// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoController.h"

#include "QmlComponentBarWidgetFactory.h"

#include <PhosphorRegistry/RegistryNotifier.h>

#include <QQmlEngine>
#include <QUrl>

using namespace PhosphorRegistry;

namespace PhosphorRegistryPluginDemo {

DemoController::DemoController(QString pluginRoot, QObject* parent)
    : QObject(parent)
    , m_pluginRoot(std::move(pluginRoot))
    , m_registry(std::make_unique<Registry<IBarWidgetFactory>>())
{
    QObject::connect(m_registry->notifier(), &RegistryNotifier::factoryRegistered, this,
                     &DemoController::factoryIdsChanged);
    QObject::connect(m_registry->notifier(), &RegistryNotifier::factoryUnregistered, this,
                     &DemoController::factoryIdsChanged);

    registerBuiltins();

    // A given root is searched alone, as a development root. Without one
    // the loader searches the standard paths.
    QList<PluginSearchPath> searchPaths;
    if (!m_pluginRoot.isEmpty()) {
        searchPaths.append(
            {m_pluginRoot + QLatin1Char('/') + PluginLoader::barWidgetSeam().name, PluginOrigin::Development});
    }
    m_loader = std::make_unique<PluginLoader>(m_registry.get(), searchPaths);
    // scanAndLoad runs the first scan synchronously, so the bar has every
    // plugin widget on its first paint.
    m_loader->scanAndLoad();
    // Show the directories actually searched. The CONSTANT property holds
    // because QML is wired up only after the constructor returns.
    QStringList searched;
    const QList<PluginSearchPath> actual = m_loader->catalog().searchPaths();
    for (const PluginSearchPath& path : actual) {
        searched.append(path.directory);
    }
    m_pluginRoot = searched.join(QStringLiteral(", "));
}

DemoController::~DemoController() = default;

void DemoController::setEngine(QQmlEngine* engine)
{
    // Idempotent on same-engine repeat; qFatal on different-engine
    // after first set (same contract as the in-process demo's
    // DemoController — see that .cpp for the long-form rationale).
    if (m_engine && m_engine != engine) {
        Q_ASSERT_X(false, "DemoController::setEngine", "DemoController::setEngine called twice with different engines");
        qFatal("DemoController::setEngine called twice with different engines");
    }
    m_engine = engine;
}

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
    if (!m_engine) {
        // Engine torn down — refuse the call rather than crash
        // inside the factory.
        return nullptr;
    }
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
