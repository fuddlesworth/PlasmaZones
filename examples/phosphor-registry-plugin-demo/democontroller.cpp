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

    m_loader = std::make_unique<PluginLoader>(m_registry.get(), m_pluginRoot);
    // PluginLoader resolves an empty input to the XDG default; sync
    // the final resolved path back into m_pluginRoot so the QML
    // pluginRoot Q_PROPERTY reflects the path the loader is actually
    // scanning. CONSTANT-property contract holds because no QML
    // engine has been wired up yet (setEngine() is called from
    // main() AFTER ctor returns); QML only sees the post-resolution
    // value.
    m_pluginRoot = m_loader->pluginRoot();
    // scanAndLoad already drives an initial synchronous scan via
    // WatchedDirectorySet::registerDirectory. The previous code
    // followed it with an explicit rescanNow() to "force a sync
    // first paint" — but the registerDirectory call is itself
    // synchronous, so the double-call was a redundant second sweep
    // of the same plugin root. One call is enough.
    m_loader->scanAndLoad();
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
