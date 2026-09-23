// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OsdController.h"

#include "QmlComponentOSDFactory.h"

#include <PhosphorRegistry/RegistryNotifier.h>

#include <QDebug>
#include <QMetaObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStringList>
#include <QVariant>

#include <memory>

using namespace PhosphorRegistry;

namespace PhosphorShellApp {

namespace {
// Registry::ids() is insertion order; the sort gives a stable alphabetical
// order to whatever enumerates the kinds.
QStringList sortedIds(const Registry<IOSDFactory>& registry)
{
    QStringList ids = registry.ids();
    ids.sort();
    return ids;
}
} // namespace

QString OsdController::moduleUri()
{
    return QStringLiteral("Phosphor.OSD");
}

const QList<OsdController::BuiltinOsd>& OsdController::builtinOsds()
{
    // Untranslated for the same reason BarController's catalogue is: no
    // surface renders these labels, and the shell tier has no i18n wiring.
    static const QList<BuiltinOsd> osds{
        {QStringLiteral("volume"), QStringLiteral("Volume"), QStringLiteral("VolumeOSD")},
        {QStringLiteral("brightness"), QStringLiteral("Brightness"), QStringLiteral("BrightnessOSD")},
        {QStringLiteral("mic"), QStringLiteral("Microphone"), QStringLiteral("MicOSD")},
        {QStringLiteral("caps"), QStringLiteral("Caps Lock"), QStringLiteral("CapsLockOSD")},
    };
    return osds;
}

OsdController::OsdController(QObject* parent)
    : QObject(parent)
{
    // Built-ins first, notifier wiring second, so construction fires no
    // no-op factoryIdsChanged. See BarController for the Replace-policy
    // reasoning behind routing both signals through one comparing slot.
    registerBuiltins();
    m_cachedIds = sortedIds(m_registry);

    QObject::connect(m_registry.notifier(), &RegistryNotifier::factoryRegistered, this,
                     &OsdController::refreshFactoryIds);
    QObject::connect(m_registry.notifier(), &RegistryNotifier::factoryUnregistered, this,
                     &OsdController::refreshFactoryIds);
}

OsdController::~OsdController() = default;

void OsdController::registerBuiltins()
{
    for (const BuiltinOsd& osd : builtinOsds()) {
        m_registry.registerFactory(std::make_shared<QmlComponentOSDFactory>(
            osd.id, osd.displayName, moduleUri(), osd.typeName, QStringList{QStringLiteral("osd")}));
    }
}

void OsdController::refreshFactoryIds()
{
    QStringList ids = sortedIds(m_registry);
    if (ids == m_cachedIds) {
        return;
    }
    m_cachedIds = std::move(ids);
    Q_EMIT factoryIdsChanged();
}

QStringList OsdController::factoryIds() const
{
    return m_cachedIds;
}

Registry<IOSDFactory>& OsdController::registry()
{
    return m_registry;
}

QQuickItem* OsdController::createOSD(const QString& kind, QQuickItem* parent)
{
    const auto factory = m_registry.factory(kind);
    if (!factory) {
        qWarning() << "OsdController: no OSD registered for kind" << kind;
        return nullptr;
    }
    if (!parent) {
        qWarning() << "OsdController: null parent for kind" << kind;
        return nullptr;
    }
    QQmlEngine* engine = qmlEngine(parent);
    if (!engine) {
        qWarning() << "OsdController: no QML engine resolvable from parent for kind" << kind;
        return nullptr;
    }
    return factory->createOSD(engine, parent);
}

void OsdController::attachHost(QObject* host)
{
    if (!host) {
        return;
    }
    for (const auto& existing : m_hosts) {
        if (existing == host) {
            return;
        }
    }
    m_hosts.append(QPointer<QObject>(host));
}

void OsdController::detachHost(QObject* host)
{
    m_hosts.removeIf([host](const QPointer<QObject>& entry) {
        return entry.isNull() || entry == host;
    });
}

int OsdController::hostCount() const
{
    int count = 0;
    for (const auto& host : m_hosts) {
        if (!host.isNull()) {
            ++count;
        }
    }
    return count;
}

bool OsdController::show(const QString& kind, int value, const QString& targetScreen)
{
    // Mirrors the demo's IPC arm: the stateful OSDs read the value as a
    // toggle, the value-based ones get `undefined` for active, which an
    // invalid QVariant becomes on the QML side.
    const bool stateful = kind == QLatin1String("mic") || kind == QLatin1String("caps");
    const QVariant active = stateful ? QVariant(value != 0) : QVariant();

    bool accepted = false;
    // Sweep dead hosts first so a reload never leaves a null in the list.
    m_hosts.removeIf([](const QPointer<QObject>& entry) {
        return entry.isNull();
    });
    for (const auto& host : m_hosts) {
        // OSDHost.show is a QML function, so it is invoked by name with
        // QVariant parameters, which is how the engine exposes it.
        QVariant result;
        const bool invoked = QMetaObject::invokeMethod(
            host.data(), "show", Qt::DirectConnection, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, kind),
            Q_ARG(QVariant, value), Q_ARG(QVariant, active), Q_ARG(QVariant, targetScreen));
        if (!invoked) {
            qWarning() << "OsdController: attached host has no show(kind, value, active, targetScreen)";
            continue;
        }
        accepted = result.toBool() || accepted;
    }
    if (m_hosts.isEmpty()) {
        qWarning() << "OsdController: no OSD host attached; cannot show" << kind;
    }
    return accepted;
}

} // namespace PhosphorShellApp
