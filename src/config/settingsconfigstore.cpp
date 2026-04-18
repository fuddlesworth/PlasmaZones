// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsconfigstore.h"

#include "settings.h"

namespace PlasmaZones {

SettingsConfigStore::SettingsConfigStore(Settings* settings, QObject* parent)
    : Phosphor::Screens::IConfigStore(parent)
    , m_settings(settings)
{
    Q_ASSERT(settings);
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
    // Settings treats an empty VirtualScreenConfig as a removal request
    // (see VirtualScreenConfig::isValid). We never need to send a
    // synthetic "delete" payload.
    return m_settings->setVirtualScreenConfig(physicalScreenId, Phosphor::Screens::VirtualScreenConfig{});
}

} // namespace PlasmaZones
