// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../plasmazones_export.h"
#include <QString>
#include <QColor>

class KConfigGroup;

namespace PlasmaZones {

class Settings;

/**
 * @brief Handles Settings persistence to/from KConfig
 *
 * SRP: This class is responsible ONLY for:
 * - Loading settings from KConfig files
 * - Saving settings to KConfig files
 * - Applying default values
 *
 * It does NOT:
 * - Own the settings data (Settings class owns that)
 * - Handle color importing (ColorImporter does that)
 * - Emit signals (Settings class does that)
 */
class PLASMAZONES_EXPORT SettingsPersistence
{
public:
    /**
     * @brief Load settings from KConfig into a Settings object
     * @param settings The Settings object to populate
     */
    static void load(Settings& settings);

    /**
     * @brief Save settings from a Settings object to KConfig
     * @param settings The Settings object to persist
     */
    static void save(const Settings& settings);

    /**
     * @brief Apply default values to a Settings object
     * @param settings The Settings object to reset
     *
     * This clears all stored config and applies ConfigDefaults values.
     */
    static void applyDefaults(Settings& settings);

private:
    // ═══════════════════════════════════════════════════════════════════════════════
    // DRY Helper Methods - Consolidate repeated validation patterns
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Read and validate an integer setting
     * @param group KConfig group to read from
     * @param key Setting key name
     * @param defaultValue Default if missing or invalid
     * @param min Minimum valid value
     * @param max Maximum valid value
     * @param settingName Human-readable name for logging
     * @return Validated integer value
     */
    static int readValidatedInt(const KConfigGroup& group, const char* key, int defaultValue,
                                int min, int max, const char* settingName);

    /**
     * @brief Read and validate a color setting
     * @param group KConfig group to read from
     * @param key Setting key name
     * @param defaultValue Default if missing or invalid
     * @param settingName Human-readable name for logging
     * @return Validated QColor value
     */
    static QColor readValidatedColor(const KConfigGroup& group, const char* key,
                                     const QColor& defaultValue, const char* settingName);

    /**
     * @brief Load an array of 9 indexed shortcuts
     * @param group KConfig group to read from
     * @param keyPattern Pattern with %1 placeholder for index (1-9)
     * @param shortcuts Output array to populate
     * @param defaults Default values array
     */
    static void loadIndexedShortcuts(const KConfigGroup& group, const QString& keyPattern,
                                     QString (&shortcuts)[9], const QString (&defaults)[9]);

    /**
     * @brief Save an array of 9 indexed shortcuts
     * @param group KConfig group to write to
     * @param keyPattern Pattern with %1 placeholder for index (1-9)
     * @param shortcuts Array of shortcuts to save
     */
    static void saveIndexedShortcuts(KConfigGroup& group, const QString& keyPattern,
                                     const QString (&shortcuts)[9]);

    // Section loaders (organized by config group)
    static void loadActivationSettings(Settings& settings, KConfigGroup& group);
    static void loadDisplaySettings(Settings& settings, KConfigGroup& group);
    static void loadAppearanceSettings(Settings& settings, KConfigGroup& group);
    static void loadZoneSettings(Settings& settings, KConfigGroup& group);
    static void loadBehaviorSettings(Settings& settings, KConfigGroup& group);
    static void loadExclusionSettings(Settings& settings, KConfigGroup& group);
    static void loadZoneSelectorSettings(Settings& settings, KConfigGroup& group);
    static void loadShaderSettings(Settings& settings, KConfigGroup& group);
    static void loadGlobalShortcuts(Settings& settings, KConfigGroup& group);
    static void loadNavigationShortcuts(Settings& settings, KConfigGroup& group);
    static void loadAutotileSettings(Settings& settings, KConfigGroup& group);
    static void loadAutotileShortcuts(Settings& settings, KConfigGroup& group);

    // Section savers (organized by config group)
    static void saveActivationSettings(const Settings& settings, KConfigGroup& group);
    static void saveDisplaySettings(const Settings& settings, KConfigGroup& group);
    static void saveAppearanceSettings(const Settings& settings, KConfigGroup& group);
    static void saveZoneSettings(const Settings& settings, KConfigGroup& group);
    static void saveBehaviorSettings(const Settings& settings, KConfigGroup& group);
    static void saveExclusionSettings(const Settings& settings, KConfigGroup& group);
    static void saveZoneSelectorSettings(const Settings& settings, KConfigGroup& group);
    static void saveShaderSettings(const Settings& settings, KConfigGroup& group);
    static void saveGlobalShortcuts(const Settings& settings, KConfigGroup& group);
    static void saveNavigationShortcuts(const Settings& settings, KConfigGroup& group);
    static void saveAutotileSettings(const Settings& settings, KConfigGroup& group);
    static void saveAutotileShortcuts(const Settings& settings, KConfigGroup& group);

    // Section defaults (organized by config group)
    static void applyActivationDefaults(Settings& settings);
    static void applyDisplayDefaults(Settings& settings);
    static void applyAppearanceDefaults(Settings& settings);
    static void applyZoneDefaults(Settings& settings);
    static void applyBehaviorDefaults(Settings& settings);
    static void applyExclusionDefaults(Settings& settings);
    static void applyZoneSelectorDefaults(Settings& settings);
    static void applyShaderDefaults(Settings& settings);
    static void applyGlobalShortcutDefaults(Settings& settings);
    static void applyNavigationShortcutDefaults(Settings& settings);
    static void applyAutotileDefaults(Settings& settings);
    static void applyAutotileShortcutDefaults(Settings& settings);
};

} // namespace PlasmaZones
