// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsconfigstore.h"

#include "settings.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSettingsConfigStore, "plasmazones.settingsconfigstore")

namespace PlasmaZones {

SettingsConfigStore::SettingsConfigStore(Settings* settings, QObject* parent)
    : Phosphor::Screens::IConfigStore(parent)
    , m_settings(settings)
{
    // Real null-guard (not Q_ASSERT — NDEBUG compiles it out and the
    // subsequent connect() would then dereference nullptr). Mirrors the
    // QPointer-guarded style every other method in this class uses: a
    // null Settings yields a no-op adapter whose load/save/remove all
    // return empty/false, rather than a crash.
    if (!settings) {
        qCWarning(lcSettingsConfigStore) << "SettingsConfigStore constructed with null Settings — adapter will no-op";
        return;
    }
    // Settings::virtualScreenConfigsChanged() is parameterless — direct
    // wire to IConfigStore::changed(). The connection captures a raw
    // pointer to settings; the QPointer guards against post-destruction
    // calls but the connect itself is lifetimed by `this`.
    connect(settings, &Settings::virtualScreenConfigsChanged, this, &Phosphor::Screens::IConfigStore::changed);
}

QHash<QString, Phosphor::Screens::VirtualScreenConfig> SettingsConfigStore::loadAll() const
{
    if (!m_settings) {
        return {};
    }
    return m_settings->virtualScreenConfigs();
}

Phosphor::Screens::VirtualScreenConfig SettingsConfigStore::get(const QString& physicalScreenId) const
{
    if (!m_settings) {
        return {};
    }
    return m_settings->virtualScreenConfig(physicalScreenId);
}

bool SettingsConfigStore::save(const QString& physicalScreenId, const Phosphor::Screens::VirtualScreenConfig& config)
{
    if (!m_settings) {
        return false;
    }
    return m_settings->setVirtualScreenConfig(physicalScreenId, config);
}

bool SettingsConfigStore::remove(const QString& physicalScreenId)
{
    if (!m_settings) {
        return false;
    }
    // Settings treats an empty Phosphor::Screens::VirtualScreenConfig as a removal request
    // (see Phosphor::Screens::VirtualScreenConfig::isValid). We never need to send a
    // synthetic "delete" payload.
    return m_settings->setVirtualScreenConfig(physicalScreenId, Phosphor::Screens::VirtualScreenConfig{});
}

} // namespace PlasmaZones
