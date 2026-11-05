// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoController.h"

#include "QmlComponentBarWidgetFactory.h"

#include <PhosphorRegistry/RegistryNotifier.h>

#include <QQmlEngine>
#include <QUrl>

using namespace PhosphorRegistry;

namespace PhosphorRegistryDemo {

DemoController::DemoController(QObject* parent)
    : QObject(parent)
    , m_registry(std::make_unique<Registry<IBarWidgetFactory>>())
{
    // Forward the registry's add/remove signals as a single
    // factoryIdsChanged so the QML side rebinds without needing to
    // listen to two distinct notifications.
    QObject::connect(m_registry->notifier(), &RegistryNotifier::factoryRegistered, this,
                     &DemoController::factoryIdsChanged);
    QObject::connect(m_registry->notifier(), &RegistryNotifier::factoryUnregistered, this,
                     &DemoController::factoryIdsChanged);

    registerBuiltins();
}

DemoController::~DemoController() = default;

void DemoController::setEngine(QQmlEngine* engine)
{
    // Idempotent on same-engine repeat — silent no-op so debug and
    // release behave identically. Different-engine after first set
    // is a contract violation that would leave dangling QML bindings
    // against the prior engine; qFatal so release builds fail loud
    // instead of silently corrupting state. The matching debug-only
    // Q_ASSERT_X catches the same case earlier with a clearer
    // backtrace under a debugger.
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
        QUrl(QStringLiteral("qrc:/qt/qml/Phosphor/RegistryDemo/ClockWidget.qml")),
        QStringList{QStringLiteral("bar.widget")}));
    m_registry->registerFactory(std::make_shared<QmlComponentBarWidgetFactory>(
        QStringLiteral("colorsquare"), QStringLiteral("Color Square"),
        QUrl(QStringLiteral("qrc:/qt/qml/Phosphor/RegistryDemo/ColorSquareWidget.qml")),
        QStringList{QStringLiteral("bar.widget")}));
}

QQuickItem* DemoController::createWidgetFor(const QString& id, QQuickItem* parent)
{
    if (!m_engine) {
        // Engine torn down (e.g. shutdown re-entry) — refuse the
        // call rather than crash inside the factory.
        return nullptr;
    }
    auto factory = m_registry->factory(id);
    if (!factory) {
        return nullptr;
    }
    return factory->createWidget(m_engine.data(), parent);
}

QStringList DemoController::factoryIds() const
{
    QStringList ids = m_registry->ids();
    // QHash iteration is unspecified — give QML a deterministic
    // alphabetical order so the bar layout doesn't reshuffle
    // across launches.
    ids.sort();
    return ids;
}

} // namespace PhosphorRegistryDemo
