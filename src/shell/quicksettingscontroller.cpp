// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuickSettingsController.h"
#include "phosphor_i18n.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QSettings>
#include <QStandardPaths>

namespace PhosphorShellApp {
namespace {
const QString kwin = QStringLiteral("org.kde.KWin");
const QString nightPath = QStringLiteral("/org/kde/KWin/NightLight");
const QString nightInterface = QStringLiteral("org.kde.KWin.NightLight");
const QString power = QStringLiteral("org.freedesktop.UPower.PowerProfiles");
const QString powerPath = QStringLiteral("/org/freedesktop/UPower/PowerProfiles");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
QDBusMessage getAll(const QString& service, const QString& path, const QString& interface)
{
    auto message = QDBusMessage::createMethodCall(service, path, properties, QStringLiteral("GetAll"));
    message << interface;
    return message;
}
}
QuickSettingsController::QuickSettingsController(QObject* parent)
    : QuickSettingsController(
          QDBusConnection::sessionBus(), QDBusConnection::systemBus(),
          QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc"), parent)
{
}
QuickSettingsController::QuickSettingsController(QDBusConnection session, QDBusConnection system, QString configPath,
                                                 QObject* parent)
    : QObject(parent)
    , m_session(std::move(session))
    , m_system(std::move(system))
    , m_configPath(std::move(configPath))
{
    auto* compositor = new QDBusServiceWatcher(kwin, m_session, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(compositor, &QDBusServiceWatcher::serviceOwnerChanged, this, &QuickSettingsController::refreshNightLight);
    auto* profiles = new QDBusServiceWatcher(power, m_system, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(profiles, &QDBusServiceWatcher::serviceOwnerChanged, this, &QuickSettingsController::refreshPower);
    for (const auto& item : {qMakePair(nightPath, kwin), qMakePair(powerPath, power)}) {
        auto& bus = item.second == kwin ? m_session : m_system;
        bus.connect(item.second, item.first, properties, QStringLiteral("PropertiesChanged"), this,
                    SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    }
    refreshNightLight();
    refreshPower();
}
void QuickSettingsController::refreshNightLight()
{
    auto* watcher = new QDBusPendingCallWatcher(m_session.asyncCall(getAll(kwin, nightPath, nightInterface)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        const bool available = !reply.isError() && reply.value().value(QStringLiteral("available")).toBool();
        const bool enabled = !reply.isError() && reply.value().value(QStringLiteral("enabled")).toBool();
        if (available != m_available || enabled != m_enabled) {
            m_available = available;
            m_enabled = enabled;
            Q_EMIT changed();
        }
    });
}
void QuickSettingsController::refreshPower()
{
    auto* watcher = new QDBusPendingCallWatcher(m_system.asyncCall(getAll(power, powerPath, power)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        const QString profile =
            reply.isError() ? QString() : reply.value().value(QStringLiteral("ActiveProfile")).toString();
        if (m_profile != profile) {
            m_profile = profile;
            Q_EMIT changed();
        }
    });
}
void QuickSettingsController::propertiesChanged(const QString& interface, const QVariantMap&, const QStringList&)
{
    if (interface == nightInterface)
        refreshNightLight();
    else if (interface == power)
        refreshPower();
}
void QuickSettingsController::toggleNightLight()
{
    if (!m_available || m_pending)
        return;
    QSettings settings(m_configPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("NightColor"));
    settings.setValue(QStringLiteral("Active"), !m_enabled);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        m_error = PhosphorI18n::tr("Could not save Night light settings");
        Q_EMIT changed();
        return;
    }
    m_pending = true;
    auto message = QDBusMessage::createMethodCall(kwin, QStringLiteral("/KWin"), kwin, QStringLiteral("reconfigure"));
    auto* watcher = new QDBusPendingCallWatcher(m_session.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        m_pending = false;
        m_error = reply.isError() ? PhosphorI18n::tr("Could not apply Night light settings") : QString();
        Q_EMIT changed();
        refreshNightLight();
    });
}
}
