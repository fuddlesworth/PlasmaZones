// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorScreens/IConfigStore.h>

#include <QPointer>

namespace PlasmaZones {

class Settings;

/**
 * @brief Daemon-side @ref Phosphor::Screens::IConfigStore implementation
 *        backed by @ref Settings.
 *
 * Settings remains the single source of truth for virtual-screen
 * configurations (config file persistence, schema validation, KCM
 * round-trip). This adapter exposes that surface through the
 * library-defined IConfigStore contract so PhosphorScreens consumers
 * (Phosphor::Screens::VirtualScreenSwapper today, ScreenManager tomorrow) can mutate the
 * same data without taking a hard dependency on Settings.
 *
 * Lifetime: holds a QPointer to Settings — the adapter survives Settings
 * destruction without dangling, and `save` / `remove` cleanly no-op when
 * Settings is gone (returns false). Callers should still keep the
 * Settings instance alive for the practical duration of any swap/rotate
 * operation.
 *
 * Signal forwarding: Settings emits `virtualScreenConfigsChanged()`
 * (parameterless) on every authoritative mutation; the adapter forwards
 * that to @ref Phosphor::Screens::IConfigStore::changed via a single
 * connect in the constructor.
 */
class PLASMAZONES_EXPORT SettingsConfigStore : public Phosphor::Screens::IConfigStore
{
    Q_OBJECT
public:
    explicit SettingsConfigStore(Settings* settings, QObject* parent = nullptr);
    ~SettingsConfigStore() override = default;

    QHash<QString, Phosphor::Screens::VirtualScreenConfig> loadAll() const override;
    Phosphor::Screens::VirtualScreenConfig get(const QString& physicalScreenId) const override;
    bool save(const QString& physicalScreenId, const Phosphor::Screens::VirtualScreenConfig& config) override;
    bool remove(const QString& physicalScreenId) override;

private:
    QPointer<Settings> m_settings;
};

} // namespace PlasmaZones
