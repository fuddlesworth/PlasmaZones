// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColorModeController.h"
#include "PhosphorShellI18n.h"
#include <PhosphorTheme/AppearanceStore.h>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QScopedValueRollback>

namespace PhosphorShellApp {
namespace {
const QString PortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString SettingsInterface = QStringLiteral("org.freedesktop.portal.Settings");
const QString AppearanceGroup = QStringLiteral("org.freedesktop.appearance");
const QString ColorSchemeKey = QStringLiteral("color-scheme");
}
ColorModeController::ColorModeController(PhosphorTheme::AppearanceStore* store, QObject* parent)
    : ColorModeController(store, QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.portal.Desktop"),
                          parent)
{
}
ColorModeController::ColorModeController(PhosphorTheme::AppearanceStore* store, const QDBusConnection& bus,
                                         const QString& service, QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_bus(bus)
    , m_service(service)
{
    connect(store, &PhosphorTheme::AppearanceStore::changed, this, [this] {
        syncAppearance();
        notifyChanged();
    });
    connect(store, &PhosphorTheme::AppearanceStore::errorChanged, this, &ColorModeController::notifyChanged);
    m_bus.connect(m_service, PortalPath, SettingsInterface, QStringLiteral("SettingChanged"), this,
                  SLOT(settingChanged(QString, QString, QDBusVariant)));
    auto* watcher = new QDBusServiceWatcher(m_service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString&, const QString& owner) {
                ++m_generation;
                if (owner.isEmpty())
                    setUnavailable(PhosphorI18n::tr(
                        "The system color preference is unavailable. Your current appearance is kept."));
                else
                    refresh();
            });
    notifyChanged();
    refresh();
}
void ColorModeController::notifyChanged()
{
    const QVariantList next{mode(), m_available, m_systemDark, effectiveDark(), previewActive(), error()};
    if (next == m_snapshot)
        return;
    m_snapshot = next;
    Q_EMIT changed();
}
QString ColorModeController::mode() const
{
    if (m_store && m_store->values().value(QStringLiteral("followSystemColorScheme")).toBool())
        return QStringLiteral("system");
    return effectiveDark() ? QStringLiteral("dark") : QStringLiteral("light");
}
bool ColorModeController::effectiveDark() const
{
    return !m_store || m_store->values().value(QStringLiteral("material")).toString() != QLatin1String("light");
}
bool ColorModeController::previewActive() const
{
    return m_store && m_store->editing();
}
QString ColorModeController::error() const
{
    return m_store && !m_store->error().isEmpty() ? m_store->error() : m_error;
}
bool ColorModeController::setMode(const QString& mode)
{
    if (!m_store || (mode == QLatin1String("system") && !m_available))
        return false;
    return m_store->setColorMode(mode, m_systemDark);
}
void ColorModeController::syncAppearance()
{
    if (!m_store || !m_available || m_syncing)
        return;
    QScopedValueRollback guard(m_syncing, true);
    m_store->updateSystemColorScheme(m_systemDark);
}
void ColorModeController::setUnavailable(const QString& error)
{
    if (!m_available && m_error == error)
        return;
    m_available = false;
    m_error = error;
    notifyChanged();
}
void ColorModeController::refresh()
{
    const auto generation = ++m_generation;
    // Read is supported by portal versions 1 and 2. Some backends return a
    // second variant wrapper, as documented by the portal specification.
    auto call = QDBusMessage::createMethodCall(m_service, PortalPath, SettingsInterface, QStringLiteral("Read"));
    call << AppearanceGroup << ColorSchemeKey;
    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusPendingReply<QDBusVariant> reply = *call;
        if (reply.isError()) {
            setUnavailable(
                PhosphorI18n::tr("The system color preference is unavailable. Your current appearance is kept."));
            return;
        }
        acceptScheme(reply.value().variant());
    });
}
void ColorModeController::acceptScheme(const QVariant& value)
{
    auto scheme = value;
    for (int i = 0; i < 3 && scheme.metaType() == QMetaType::fromType<QDBusVariant>(); ++i)
        scheme = scheme.value<QDBusVariant>().variant();
    if (scheme.metaType().id() != QMetaType::UInt) {
        setUnavailable(PhosphorI18n::tr("The system returned an invalid color preference."));
        return;
    }
    const bool dark = scheme.toUInt() == 1;
    const bool changedState = !m_available || m_systemDark != dark || !m_error.isEmpty();
    m_available = true;
    m_systemDark = dark;
    m_error.clear();
    syncAppearance();
    if (changedState)
        notifyChanged();
}
void ColorModeController::settingChanged(const QString& group, const QString& key, const QDBusVariant& value)
{
    if (group != AppearanceGroup || key != ColorSchemeKey)
        return;
    ++m_generation; // A live signal supersedes any older in-flight snapshot.
    acceptScheme(value.variant());
}
}
