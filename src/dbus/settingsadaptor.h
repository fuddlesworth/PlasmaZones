// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusVariant>
#include <QEventLoop>
#include <QString>
#include <QVariant>
#include <QTimer>
#include <functional>
#include <QHash>

namespace PlasmaZones {

class ISettings;
class Settings; // Forward declaration for concrete type

/**
 * @brief D-Bus adaptor for settings operations
 *
 * Provides D-Bus interface: org.plasmazones.Settings
 *  Settings read/write operations
 *
 * Uses registry pattern for setSetting.
 */
class PLASMAZONES_EXPORT SettingsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Settings")

public:
    explicit SettingsAdaptor(ISettings* settings, QObject* parent = nullptr);
    ~SettingsAdaptor() override;

public Q_SLOTS:
    // Settings operations
    void reloadSettings();
    void saveSettings();
    void resetToDefaults();

    // Generic get/set (registry-based)
    QString getAllSettings();
    QDBusVariant getSetting(const QString& key);
    bool setSetting(const QString& key, const QDBusVariant& value);

    /**
     * @brief Batch-get multiple settings in one D-Bus call.
     *
     * Reads every requested key via the getter registry and returns a map
     * containing only the keys that were found. Unknown keys are logged as
     * warnings and omitted from the result (callers should fall back to
     * their hardcoded defaults for missing entries).
     *
     * Exists to collapse the editor startup's per-key getSetting() chain
     * (8 round-trips for gap/overlay settings) into a single round-trip.
     *
     * @param keys List of setting keys to fetch
     * @return Map of key -> value for every key that was found
     */
    QVariantMap getSettings(const QStringList& keys);

    /**
     * @brief Batch-set multiple settings in one D-Bus call.
     *
     * Applies all values via the setter registry, saves once (synchronously),
     * and lets the KConfig change notification propagate settingsChanged.
     * Unknown keys are logged as warnings but do not abort the batch.
     *
     * @param settings Map of setting key -> value
     * @return true if every key was found in the registry and its setter succeeded
     */
    bool setSettings(const QVariantMap& settings);

    QStringList getSettingKeys();

    // Per-screen settings (categories: "autotile", "snapping", "zoneSelector")
    void setPerScreenSetting(const QString& screenId, const QString& category, const QString& key,
                             const QDBusVariant& value);
    void clearPerScreenSettings(const QString& screenId, const QString& category);
    QVariantMap getPerScreenSettings(const QString& screenId, const QString& category);

    /**
     * @brief Get list of available shader effects
     * @return List of shader metadata (id, name, description, etc.)
     */
    QVariantList availableShaders();

    /**
     * @brief Get detailed information about a specific shader
     * @param shaderId UUID of the shader to query
     * @return Shader metadata map, or empty map if not found
     */
    QVariantMap shaderInfo(const QString& shaderId);

    /**
     * @brief Get default parameter values for a shader
     * @param shaderId UUID of the shader to query
     * @return Map of parameter IDs to default values
     */
    QVariantMap defaultShaderParams(const QString& shaderId);

    /**
     * @brief Translate shader params from param IDs to uniform names for ZoneShaderItem
     * @param shaderId UUID of the shader
     * @param params Map of param IDs to values (e.g. {"intensity": 0.5})
     * @return Map of uniform names to values (e.g. {"customParams1_x": 0.5})
     */
    QVariantMap translateShaderParams(const QString& shaderId, const QVariantMap& params);

    /**
     * @brief Check if shader effects are enabled (compiled with shader support)
     * @return true if shaders are available
     */
    bool shadersEnabled();

    /**
     * @brief Check if user-installed shaders are supported
     * @return true if user shaders can be loaded
     */
    bool userShadersEnabled();

    /**
     * @brief Get the user shader installation directory path
     * @return Path to ~/.local/share/plasmazones/shaders
     */
    QString userShaderDirectory();

    /**
     * @brief Open the user shader directory in the file manager
     */
    void openUserShaderDirectory();

    /**
     * @brief Refresh the shader registry (reload all shaders)
     */
    void refreshShaders();

    /**
     * @brief Get list of currently running windows (for exclusion picker)
     *
     * DEPRECATED — blocks up to 2 seconds on a QEventLoop waiting for
     * provideRunningWindows() to fire. Replaced by the fire-and-forget
     * requestRunningWindows() + runningWindowsAvailable(json) signal
     * pair, which keeps the settings UI responsive even when the KWin
     * effect is unloaded or slow to respond. New callers MUST use the
     * async pair. This method is kept only so in-flight clients on
     * older daemon versions still work while they migrate; it will be
     * removed once every caller is on the async flow.
     *
     * @return JSON string: [{"windowClass":"...", "appName":"...", "caption":"..."}]
     */
    QString getRunningWindows();

    /**
     * @brief Asynchronous window list request (fire-and-forget).
     *
     * Emits runningWindowsRequested() so the KWin effect enumerates its
     * window list and sends it back via provideRunningWindows(). The
     * resulting JSON is then broadcast via the runningWindowsAvailable
     * signal — subscribers update their view when the signal arrives
     * rather than blocking on the D-Bus call.
     *
     * Returns immediately. Safe to call repeatedly from UI — each call
     * triggers at most one KWin round-trip; duplicate requests while a
     * previous response is in flight are coalesced by the effect side.
     */
    void requestRunningWindows();

    /**
     * @brief Receive window list from KWin effect (callback)
     * @param json JSON array of window objects
     */
    void provideRunningWindows(const QString& json);

    /**
     * @brief Get metadata for a single setting
     * @param key Setting key name
     * @return JSON: {key, type} — type is "bool"|"int"|"double"|"string"|"color"|"stringlist"
     */
    QString getSettingSchema(const QString& key);

    /**
     * @brief Get metadata for all settings
     * @return JSON object: {key: {type}, ...}
     */
    QString getAllSettingSchemas();

Q_SIGNALS:
    void settingsChanged();

    /**
     * @brief Daemon → KWin effect: please enumerate running windows.
     *
     * Emitted by requestRunningWindows() (async path) and by the legacy
     * blocking getRunningWindows() (migration path). The effect answers
     * by calling provideRunningWindows().
     */
    void runningWindowsRequested();

    /**
     * @brief Daemon → clients: fresh running-windows JSON is available.
     *
     * Emitted every time provideRunningWindows() receives a payload from
     * the KWin effect. Subscribers (SettingsController) cache the value
     * and present it to the UI without a blocking round-trip.
     *
     * @param json JSON array: [{windowClass, appName, caption}, ...]
     */
    void runningWindowsAvailable(const QString& json);

private:
    void initializeRegistry();

    /**
     * @brief Schedule a debounced save
     *
     * Performance optimization: Batches multiple setting changes into a single
     * save operation instead of writing to disk on every change.
     */
    void scheduleSave();

    /**
     * @brief Drop all cached ShaderRegistry results.
     *
     * Called from refreshShaders() and from ShaderRegistry::shadersChanged
     * so the editor and KCM never see stale shader metadata after a
     * hot-reload. Cheap — just clears three hashes.
     */
    void invalidateShaderCaches();

    ISettings* m_settings; // Interface type (DIP)

    // Window picker request/response state
    QString m_pendingWindowList;
    QEventLoop* m_windowListLoop = nullptr;

    // Registry pattern
    using Getter = std::function<QVariant()>;
    using Setter = std::function<bool(const QVariant&)>;

    QHash<QString, Getter> m_getters;
    QHash<QString, Setter> m_setters;
    QHash<QString, QString> m_schemas; // key -> type ("bool"|"int"|"double"|"string"|"color"|"stringlist")

    // Debounced save timer (performance optimization)
    QTimer* m_saveTimer = nullptr;
    static constexpr int SaveDebounceMs = 500; // 500ms debounce

    // ═══════════════════════════════════════════════════════════════════════
    // ShaderRegistry caches
    //
    // Memoizes availableShaders() / shaderInfo() / defaultShaderParams() so
    // repeated editor + KCM queries don't hit ShaderRegistry on every call.
    // Invalidated on refreshShaders() and on the registry's own shadersChanged
    // signal. Marked mutable so const-ish read paths can populate them.
    // ═══════════════════════════════════════════════════════════════════════
    QVariantList m_cachedAvailableShaders;
    bool m_cachedAvailableShadersValid = false;
    QHash<QString, QVariantMap> m_cachedShaderInfo;
    QHash<QString, QVariantMap> m_cachedShaderDefaults;
};

} // namespace PlasmaZones
