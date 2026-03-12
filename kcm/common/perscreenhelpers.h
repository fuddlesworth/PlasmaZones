// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>
#include "../../src/core/utils.h"

namespace PlasmaZones::PerScreen {

/**
 * @brief Get per-screen settings for a given screen using a member-function-pointer getter
 * @tparam SettingsT The concrete Settings type
 * @tparam Getter A member function pointer: QVariantMap (SettingsT::*)(const QString&) const
 */
template<typename SettingsT, typename Getter>
QVariantMap get(const SettingsT* settings, const QString& screenName, Getter getter)
{
    return settings ? (settings->*getter)(Utils::screenIdForName(screenName)) : QVariantMap();
}

/**
 * @brief Set a single per-screen setting key/value using a member-function-pointer setter
 * @tparam SettingsT The concrete Settings type
 * @tparam Setter A member function pointer: void (SettingsT::*)(const QString&, const QString&, const QVariant&)
 */
template<typename SettingsT, typename Setter>
void set(SettingsT* settings, const QString& screenName, const QString& key, const QVariant& value, Setter setter)
{
    if (!settings || screenName.isEmpty())
        return;
    (settings->*setter)(Utils::screenIdForName(screenName), key, value);
}

/**
 * @brief Clear all per-screen settings for a given screen
 * @tparam SettingsT The concrete Settings type
 * @tparam Clearer A member function pointer: void (SettingsT::*)(const QString&)
 */
template<typename SettingsT, typename Clearer>
void clear(SettingsT* settings, const QString& screenName, Clearer clearer)
{
    if (!settings || screenName.isEmpty())
        return;
    (settings->*clearer)(Utils::screenIdForName(screenName));
}

/**
 * @brief Check whether per-screen settings exist for a given screen
 * @tparam SettingsT The concrete Settings type
 * @tparam Checker A member function pointer: bool (SettingsT::*)(const QString&) const
 */
template<typename SettingsT, typename Checker>
bool has(const SettingsT* settings, const QString& screenName, Checker checker)
{
    return settings ? (settings->*checker)(Utils::screenIdForName(screenName)) : false;
}

} // namespace PlasmaZones::PerScreen
