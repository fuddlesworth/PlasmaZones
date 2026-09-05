// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "shortcutcatalog.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcShortcutCatalog, "phosphordashboard.shortcuts")

namespace PhosphorShellDashboard {

namespace {
using PhosphorProtocol::Service::Name;
using PhosphorProtocol::Service::ObjectPath;
constexpr QLatin1String KeyTriggers("triggers");
} // namespace

ShortcutCatalog::ShortcutCatalog(QObject* parent)
    : QObject(parent)
    , m_watcher(new QDBusServiceWatcher(
          Name, QDBusConnection::sessionBus(),
          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this))
{
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        setAvailable(true);
    });
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        setAvailable(false);
    });
    // The rebind announcement. An older daemon never emits it, which only
    // means an open sheet reflects a rebind on its next open.
    QDBusConnection::sessionBus().connect(Name, ObjectPath, PhosphorProtocol::Service::Interface::Control,
                                          QStringLiteral("shortcutsChanged"), this, SLOT(refresh()));
    setAvailable(QDBusConnection::sessionBus().interface()->isServiceRegistered(Name));
}

ShortcutCatalog::~ShortcutCatalog() = default;

bool ShortcutCatalog::isAvailable() const
{
    return m_available;
}

QVariantList ShortcutCatalog::rows() const
{
    return m_rows;
}

void ShortcutCatalog::setAvailable(bool available)
{
    if (available == m_available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged();
    if (available) {
        refresh();
    } else {
        ++m_generation;
        setRows({});
    }
}

void ShortcutCatalog::setRows(const QVariantList& rows)
{
    if (rows == m_rows) {
        return;
    }
    m_rows = rows;
    Q_EMIT rowsChanged();
}

void ShortcutCatalog::refresh()
{
    if (!m_available) {
        return;
    }
    const int generation = ++m_generation;
    QDBusMessage msg = QDBusMessage::createMethodCall(Name, ObjectPath, PhosphorProtocol::Service::Interface::Control,
                                                      QStringLiteral("getShortcutsJson"));
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (generation != m_generation) {
            return;
        }
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            // Older daemon without the read: the sheet shows every chord
            // unbound rather than nothing at all.
            qCDebug(lcShortcutCatalog) << "getShortcutsJson:" << reply.error().message();
            setRows({});
            return;
        }
        setRows(parseRows(reply.value()));
    });
}

QVariantList ShortcutCatalog::parseRows(const QString& json)
{
    QVariantList rows;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) {
        return rows;
    }
    const QJsonArray array = doc.array();
    rows.reserve(array.size());
    for (const QJsonValue& v : array) {
        if (!v.isObject()) {
            continue;
        }
        QVariantMap row = v.toObject().toVariantMap();
        // A string list rather than a variant list, so QML's `join` and
        // `length` read it directly.
        row.insert(KeyTriggers, row.value(KeyTriggers).toStringList());
        rows.append(row);
    }
    return rows;
}

} // namespace PhosphorShellDashboard
