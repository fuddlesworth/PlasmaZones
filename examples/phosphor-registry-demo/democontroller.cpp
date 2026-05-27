// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoController.h"

#include "QmlComponentBarWidgetFactory.h"

#include <PhosphorRegistry/RegistryNotifier.h>

#include <QQmlEngine>
#include <QUrl>

using namespace PhosphorRegistry;

namespace PhosphorRegistryDemo {

DemoController::DemoController(QQmlEngine* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
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
