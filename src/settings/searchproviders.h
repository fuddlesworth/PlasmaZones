// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/ISearchProvider.h>
#include <PhosphorControl/SearchEntry.h>

#include <QVector>

namespace PlasmaZones {

class SettingsController;

/**
 * @brief ISearchProvider feeding user layouts into the global search index.
 *
 * Snapshots `SettingsController::layouts()` on demand; results navigate to the
 * Layouts page. Re-snapshot is driven by the controller's `layoutsChanged`
 * signal wired to SearchController::invalidate in main.cpp.
 */
class LayoutsSearchProvider : public PhosphorControl::ISearchProvider
{
public:
    explicit LayoutsSearchProvider(SettingsController* controller)
        : m_controller(controller)
    {
    }

    QVector<PhosphorControl::SearchEntry> searchEntries() const override;

private:
    SettingsController* m_controller = nullptr;
};

/**
 * @brief ISearchProvider feeding rules into the global search index.
 *
 * Snapshots the RuleModel (name + match summary); results navigate to the
 * Rules page. Re-snapshot is driven by the model's `countChanged` (add /
 * remove) and `dataChanged` (in-place edits like a rename or summary change).
 */
class RulesSearchProvider : public PhosphorControl::ISearchProvider
{
public:
    explicit RulesSearchProvider(SettingsController* controller)
        : m_controller(controller)
    {
    }

    QVector<PhosphorControl::SearchEntry> searchEntries() const override;

private:
    SettingsController* m_controller = nullptr;
};

/**
 * @brief ISearchProvider feeding saved profiles into the global search index.
 *
 * Snapshots `ProfileStore::availableProfiles()` (name + what it inherits from);
 * results navigate to the Profiles page and reveal the individual row, which
 * registers a "profile:<id>" anchor. Re-snapshot is driven by the store's
 * `profilesChanged` signal wired to SearchController::invalidate in main.cpp,
 * so a rename, import, or delete is reflected without a restart.
 */
class ProfilesSearchProvider : public PhosphorControl::ISearchProvider
{
public:
    explicit ProfilesSearchProvider(SettingsController* controller)
        : m_controller(controller)
    {
    }

    QVector<PhosphorControl::SearchEntry> searchEntries() const override;

private:
    SettingsController* m_controller = nullptr;
};

} // namespace PlasmaZones
