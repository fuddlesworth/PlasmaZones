// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BarController.h"

#include "QmlComponentBarWidgetFactory.h"

#include <PhosphorRegistry/RegistryNotifier.h>

#include <QDebug>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStringList>

#include <memory>

using namespace PhosphorRegistry;

namespace PhosphorShellApp {

namespace {
// All built-in bar widgets live in the Phosphor.Bar module.
const QString kModule = QStringLiteral("Phosphor.Bar");
} // namespace

BarController::BarController(QObject* parent)
    : QObject(parent)
{
    // Forward the registry's add/remove signals as a single
    // factoryIdsChanged so the QML side rebinds without listening to two
    // distinct notifications (a future plugin loader / hot-reload can add
    // or drop widgets at runtime).
    QObject::connect(m_registry.notifier(), &RegistryNotifier::factoryRegistered, this,
                     &BarController::factoryIdsChanged);
    QObject::connect(m_registry.notifier(), &RegistryNotifier::factoryUnregistered, this,
                     &BarController::factoryIdsChanged);

    registerBuiltins();
}

BarController::~BarController() = default;

void BarController::registerBuiltins()
{
    // Register each built-in bar widget as an IBarWidgetFactory wrapping a
    // delegate type from the Phosphor.Bar module. Capabilities are advisory
    // (enforced in Phase 5). Workspaces is intentionally absent: it has no
    // shipped data source yet (no QML-exposed workspace model), and faking
    // it with hardcoded indices is disallowed; it lands with a real
    // compositor workspace source.
    const auto reg = [this](const QString& id, const QString& name, const QString& type) {
        m_registry.registerFactory(std::make_shared<QmlComponentBarWidgetFactory>(
            id, name, kModule, type, QStringList{QStringLiteral("bar.widget")}));
    };
    reg(QStringLiteral("clock"), QStringLiteral("Clock"), QStringLiteral("Clock"));
    reg(QStringLiteral("focusedapp"), QStringLiteral("Focused App"), QStringLiteral("FocusedApp"));
    reg(QStringLiteral("systemmetrics"), QStringLiteral("System Metrics"), QStringLiteral("SystemMetrics"));
    reg(QStringLiteral("network"), QStringLiteral("Network"), QStringLiteral("Network"));
    reg(QStringLiteral("bluetooth"), QStringLiteral("Bluetooth"), QStringLiteral("Bluetooth"));
    reg(QStringLiteral("audio"), QStringLiteral("Audio"), QStringLiteral("Audio"));
    reg(QStringLiteral("battery"), QStringLiteral("Battery"), QStringLiteral("Battery"));
    reg(QStringLiteral("tray"), QStringLiteral("System Tray"), QStringLiteral("Tray"));
    reg(QStringLiteral("media"), QStringLiteral("Media"), QStringLiteral("Media"));
    reg(QStringLiteral("controlcenter"), QStringLiteral("Control Center"), QStringLiteral("ControlCenterButton"));
    reg(QStringLiteral("notification"), QStringLiteral("Notifications"), QStringLiteral("NotificationButton"));
    reg(QStringLiteral("power"), QStringLiteral("Power"), QStringLiteral("PowerButton"));
    reg(QStringLiteral("spacer"), QStringLiteral("Spacer"), QStringLiteral("Spacer"));
}

QQuickItem* BarController::createWidgetFor(const QString& id, QQuickItem* parent)
{
    const auto factory = m_registry.factory(id);
    if (!factory) {
        qWarning() << "BarController: no bar widget registered for id" << id;
        return nullptr;
    }
    QQmlEngine* engine = parent ? qmlEngine(parent) : nullptr;
    if (!engine) {
        qWarning() << "BarController: no QML engine resolvable from parent for id" << id;
        return nullptr;
    }
    return factory->createWidget(engine, parent);
}

QStringList BarController::factoryIds() const
{
    QStringList ids = m_registry.ids();
    // QHash iteration is unspecified — give QML a deterministic
    // alphabetical order so any consumer enumerating ids is stable across
    // launches.
    ids.sort();
    return ids;
}

} // namespace PhosphorShellApp
