// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSettingsUi/DBusBridge.h"

#include <QDebug>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusPendingCall>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>

namespace PhosphorSettingsUi {

namespace {

// CLAUDE.md "Input validation at system boundaries" — D-Bus is a
// system boundary. An empty service/path/interface produces a
// malformed QDBusMessage that fails silently in async mode and
// returns an empty QDBusMessage in sync mode; reject explicitly so
// the programmer error surfaces in the log instead of buried in a
// no-op return.
bool validateEndpoint(const DBusEndpoint& endpoint, const QString& interface, const QString& method,
                      const char* callSite)
{
    if (endpoint.service.isEmpty()) {
        qWarning() << callSite << ": refusing call — empty service";
        return false;
    }
    if (endpoint.objectPath.isEmpty()) {
        qWarning() << callSite << ": refusing call — empty objectPath";
        return false;
    }
    if (interface.isEmpty()) {
        qWarning() << callSite << ": refusing call — empty interface";
        return false;
    }
    if (method.isEmpty()) {
        qWarning() << callSite << ": refusing call — empty method";
        return false;
    }
    return true;
}

} // namespace

DBusBridge::DBusBridge(DBusEndpoint endpoint, QObject* parent)
    : QObject(parent)
    , m_endpoint(std::move(endpoint))
{
    // QDBus interprets non-positive sync timeouts inconsistently
    // across Qt 6 patch releases (some treat <=0 as block-forever,
    // others as instant-fail). Clamp to a positive default so
    // syncTimeoutMs is always a usable value.
    if (m_endpoint.syncTimeoutMs <= 0) {
        qWarning() << "PhosphorSettingsUi::DBusBridge: non-positive syncTimeoutMs" << m_endpoint.syncTimeoutMs
                   << "— clamping to default 500ms";
        m_endpoint.syncTimeoutMs = 500;
    }
}

DBusBridge::~DBusBridge() = default;

DBusEndpoint DBusBridge::endpoint() const
{
    return m_endpoint;
}

QDBusMessage DBusBridge::call(const QString& method, const QVariantList& args) const
{
    return callOn(m_endpoint.interface, method, args);
}

QDBusMessage DBusBridge::callOn(const QString& interface, const QString& method, const QVariantList& args) const
{
    if (!validateEndpoint(m_endpoint, interface, method, "PhosphorSettingsUi::DBusBridge::callOn")) {
        return QDBusMessage::createError(QDBusError::InvalidArgs, QStringLiteral("DBusBridge: invalid call inputs"));
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(m_endpoint.service, m_endpoint.objectPath, interface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, m_endpoint.syncTimeoutMs);
}

void DBusBridge::asyncCall(const QString& method, const QVariantList& args) const
{
    asyncCallOn(m_endpoint.interface, method, args);
}

void DBusBridge::asyncCallOn(const QString& interface, const QString& method, const QVariantList& args) const
{
    if (!validateEndpoint(m_endpoint, interface, method, "PhosphorSettingsUi::DBusBridge::asyncCallOn")) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(m_endpoint.service, m_endpoint.objectPath, interface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    // Without a watcher, async failures (service not present, method
    // not found, type mismatch) are completely swallowed. Attach one
    // that logs and deletes itself — keeps the "fire-and-forget"
    // ergonomics for callers while making real errors visible.
    auto* watcher = new QDBusPendingCallWatcher(pending);
    QObject::connect(
        watcher, &QDBusPendingCallWatcher::finished, watcher, [interface, method](QDBusPendingCallWatcher* w) {
            QDBusPendingReply<> reply = *w;
            if (reply.isError()) {
                qWarning() << "PhosphorSettingsUi::DBusBridge::asyncCallOn: D-Bus error on" << interface << method
                           << "—" << reply.error().name() << reply.error().message();
            }
            w->deleteLater();
        });
}

} // namespace PhosphorSettingsUi
