// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "shortcutcatalog.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>

Q_LOGGING_CATEGORY(lcShortcutCatalog, "phosphordashboard.shortcuts")

namespace PhosphorShellDashboard {
namespace {
using PhosphorProtocol::Service::Name;
using PhosphorProtocol::Service::ObjectPath;
constexpr QLatin1String KeyTriggers("triggers");

bool validDocument(const QString& json)
{
    const auto document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isArray())
        return false;
    QSet<QString> ids;
    for (const auto& value : document.array()) {
        if (!value.isObject())
            return false;
        const auto row = value.toObject();
        const auto id = row.value(QLatin1String("id")).toString();
        if (id.isEmpty() || ids.contains(id) || !row.value(KeyTriggers).isArray())
            return false;
        ids.insert(id);
        for (const auto& trigger : row.value(KeyTriggers).toArray()) {
            if (!trigger.isString())
                return false;
        }
    }
    return true;
}
} // namespace

ShortcutCatalog::ShortcutCatalog(QObject* parent)
    : QObject(parent)
    , m_watcher(
          new QDBusServiceWatcher(Name, QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this))
{
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString&, const QString& owner) {
                setOwner(owner);
            });
    QDBusConnection::sessionBus().connect(Name, ObjectPath, PhosphorProtocol::Service::Interface::Control,
                                          QStringLiteral("shortcutsChanged"), this, SLOT(refresh()));
    retry();
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

void ShortcutCatalog::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    Q_EMIT loadingChanged();
}

void ShortcutCatalog::setError(const QString& error)
{
    if (m_error == error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

void ShortcutCatalog::setOwner(const QString& owner)
{
    // An owner replacement can leave available=true. It must still discard
    // the former daemon's bindings and invalidate every outstanding reply.
    const auto generation = ++m_generation;
    const bool available = !owner.isEmpty();
    const auto error = available
        ? QString()
        : QCoreApplication::translate("PhosphorShellDashboard", "The shortcut service could not be reached.");
    const bool availabilityChanged = m_available != available;
    const bool rowsDiffer = !m_rows.isEmpty();
    const bool loadingDiffers = m_loading != available;
    const bool errorDiffers = m_error != error;
    m_owner = owner;
    m_rows.clear();
    m_available = available;
    m_loading = available;
    m_error = error;
    if (availabilityChanged)
        Q_EMIT availableChanged();
    if (rowsDiffer)
        Q_EMIT rowsChanged();
    if (loadingDiffers)
        Q_EMIT loadingChanged();
    if (errorDiffers)
        Q_EMIT errorChanged();
    if (available && generation == m_generation)
        refresh();
}

void ShortcutCatalog::finishRequest(quint64 generation, const QVariantList& rows, const QString& error)
{
    if (generation != m_generation)
        return;
    const bool rowsDiffer = m_rows != rows;
    const bool loadingDiffers = m_loading;
    const bool errorDiffers = m_error != error;
    m_rows = rows;
    m_loading = false;
    m_error = error;
    // Notify only after committing the complete response. A rowsChanged
    // handler may start another fetch, whose loading state must survive.
    if (rowsDiffer)
        Q_EMIT rowsChanged();
    if (loadingDiffers)
        Q_EMIT loadingChanged();
    if (errorDiffers)
        Q_EMIT errorChanged();
}

void ShortcutCatalog::retry()
{
    const auto generation = ++m_generation;
    setLoading(true);
    setError({});
    if (generation != m_generation)
        return;
    // Resolve without activation, including on explicit retry. This cannot
    // launch an installed daemon when a nested shell or test has none.
    auto message =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                       QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    message.setArguments({Name});
    message.setAutoStartService(false);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusPendingReply<QString> reply = *call;
        setOwner(reply.isError() ? QString() : reply.value());
    });
}

void ShortcutCatalog::refresh()
{
    if (m_owner.isEmpty()) {
        retry();
        return;
    }
    const auto generation = ++m_generation;
    setLoading(true);
    setError({});
    if (generation != m_generation)
        return;
    // Address the unique owner so a delayed call cannot silently reach its
    // replacement under the same well-known name.
    auto message = QDBusMessage::createMethodCall(m_owner, ObjectPath, PhosphorProtocol::Service::Interface::Control,
                                                  QStringLiteral("getShortcutsJson"));
    message.setAutoStartService(false);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusPendingReply<QString> reply = *call;
        if (reply.isError()) {
            qCDebug(lcShortcutCatalog) << "getShortcutsJson:" << reply.error().message();
            finishRequest(generation, {},
                          QCoreApplication::translate("PhosphorShellDashboard",
                                                      "Your shortcuts could not be loaded. Try again."));
        } else if (!validDocument(reply.value())) {
            finishRequest(generation, {},
                          QCoreApplication::translate("PhosphorShellDashboard",
                                                      "The shortcut service returned an invalid catalog."));
        } else {
            finishRequest(generation, parseRows(reply.value()), {});
        }
    });
}

QVariantList ShortcutCatalog::parseRows(const QString& json)
{
    QVariantList rows;
    const auto document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isArray())
        return rows;
    for (const auto& value : document.array()) {
        if (!value.isObject())
            continue;
        QVariantMap row = value.toObject().toVariantMap();
        row.insert(KeyTriggers, row.value(KeyTriggers).toStringList());
        rows.append(row);
    }
    return rows;
}
} // namespace PhosphorShellDashboard
