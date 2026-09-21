// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ToastController.h"

#include <QDebug>
#include <QMetaObject>
#include <QVariant>
#include <QVariantMap>

namespace PhosphorShellApp {

ToastController::ToastController(QObject* parent)
    : QObject(parent)
{
}

ToastController::~ToastController() = default;

void ToastController::attachHost(QObject* host, const QString& screenName, bool primary)
{
    if (!host) {
        return;
    }
    for (Host& existing : m_hosts) {
        if (existing.object == host) {
            existing.screenName = screenName;
            existing.primary = primary;
            return;
        }
    }
    m_hosts.append(Host{QPointer<QObject>(host), screenName, primary});
}

void ToastController::detachHost(QObject* host)
{
    m_hosts.removeIf([host](const Host& entry) {
        return entry.object.isNull() || entry.object == host;
    });
}

int ToastController::hostCount() const
{
    int count = 0;
    for (const Host& host : m_hosts) {
        if (!host.object.isNull()) {
            ++count;
        }
    }
    return count;
}

QObject* ToastController::primaryHost()
{
    // Sweep dead hosts first so a reload never leaves a null in the list.
    m_hosts.removeIf([](const Host& entry) {
        return entry.object.isNull();
    });
    for (const Host& host : m_hosts) {
        if (host.primary) {
            return host.object.data();
        }
    }
    return m_hosts.isEmpty() ? nullptr : m_hosts.first().object.data();
}

int ToastController::send(const QString& summary, const QString& body)
{
    QObject* host = primaryHost();
    if (!host) {
        qWarning() << "ToastController: no toast host attached; dropping" << summary;
        return -1;
    }
    // The shape ToastHost.show takes. No appName: a wire call has no app
    // behind it, and the card hides an empty one.
    QVariantMap toast;
    toast.insert(QStringLiteral("summary"), summary);
    toast.insert(QStringLiteral("body"), body);
    toast.insert(QStringLiteral("urgency"), 1);
    // ToastHost.show is a QML function, so it is invoked by name with a
    // QVariant parameter, which is how the engine exposes it.
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(host, "show", Qt::DirectConnection, Q_RETURN_ARG(QVariant, result),
                                                   Q_ARG(QVariant, QVariant(toast)));
    if (!invoked) {
        qWarning() << "ToastController: attached host has no show(toast)";
        return -1;
    }
    bool ok = false;
    const int id = result.toInt(&ok);
    return ok ? id : -1;
}

} // namespace PhosphorShellApp
