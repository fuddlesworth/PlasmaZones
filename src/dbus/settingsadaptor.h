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
    QStringList getSettingKeys();

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
     * Requests the KWin effect to enumerate all windows. Returns JSON array
     * of objects with windowClass and caption fields.
     * Blocks up to 2 seconds waiting for the KWin effect to respond.
     *
     * @return JSON string: [{"windowClass":"...", "appName":"...", "caption":"..."}]
     */
    QString getRunningWindows();

    /**
     * @brief Receive window list from KWin effect (callback)
     * @param json JSON array of window objects
     */
    void provideRunningWindows(const QString& json);

Q_SIGNALS:
    void settingsChanged();
    void runningWindowsRequested();

private:
    void initializeRegistry();
    
    /**
     * @brief Schedule a debounced save
     * 
     * Performance optimization: Batches multiple setting changes into a single
     * save operation instead of writing to disk on every change.
     */
    void scheduleSave();

    ISettings* m_settings; // Interface type (DIP)

    // Window picker request/response state
    QString m_pendingWindowList;
    QEventLoop* m_windowListLoop = nullptr;

    // Registry pattern
    using Getter = std::function<QVariant()>;
    using Setter = std::function<bool(const QVariant&)>;

    QHash<QString, Getter> m_getters;
    QHash<QString, Setter> m_setters;
    
    // Debounced save timer (performance optimization)
    QTimer* m_saveTimer = nullptr;
    static constexpr int SaveDebounceMs = 500; // 500ms debounce
};

} // namespace PlasmaZones
