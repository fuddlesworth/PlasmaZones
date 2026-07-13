// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QDBusVariant>
#include <QString>
#include <QVariant>
#include <QTimer>
#include <functional>
#include <QHash>

namespace PhosphorAnimation {
class PhosphorProfileRegistry;
}

namespace PlasmaZones {

class ISettings;
class Settings; // Forward declaration for concrete type
class ShaderRegistry;

/**
 * @brief D-Bus adaptor for settings operations
 *
 * Provides D-Bus interface: org.plasmazones.Settings
 *  Settings read/write operations
 *
 * Uses registry pattern for setSetting.
 */
/// Inherits QDBusContext (like OverlayAdaptor) so getSetting can answer an
/// UNKNOWN key with a real D-Bus error instead of a valid-but-empty value.
/// That distinction is load-bearing: getSetting resolves keys through a
/// hand-maintained registry, not through Qt property reflection, so a setting
/// whose registration is forgotten is a live possibility — and while the miss
/// answered with an empty string, every one of the ~50 callers coerced it blind
/// (QVariant("").toBool() is false), silently shipping the default-inverted
/// value instead of failing. An error reply makes loadSettingAsync's
/// reply.isValid() false, so the callback never runs and the caller keeps its
/// own default. One central guard instead of a type-check at every call site.
class PLASMAZONES_EXPORT SettingsAdaptor : public QDBusAbstractAdaptor, public QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Settings")

public:
    /// @param settings Settings interface (required).
    /// @param shaderRegistry Per-process shader registry. Borrowed; must
    ///        outlive the adaptor. Optional in tests / unit fixtures —
    ///        when null, every shader-related method returns an empty
    ///        result and the on-disk hot-reload connection is skipped.
    /// @param profileRegistry Per-process motion-profile registry holding
    ///        the merged per-event `PhosphorAnimation::Profile` set (the
    ///        same registry the daemon's SurfaceAnimator path resolves
    ///        durations from). Borrowed; must outlive the adaptor.
    ///        Optional — when null, the `motionProfileTree` getter is not
    ///        registered and consumers fall back to the global duration.
    /// @param parent Qt parent (D-Bus adaptors are owned by their adapted
    ///        QObject via Qt parent-child).
    explicit SettingsAdaptor(ISettings* settings, ShaderRegistry* shaderRegistry = nullptr,
                             PhosphorAnimation::PhosphorProfileRegistry* profileRegistry = nullptr,
                             QObject* parent = nullptr);
    ~SettingsAdaptor() override;

    /// Null the borrowed ISettings / ShaderRegistry pointers, sever their
    /// signal wiring, and flush any pending debounced save. Called from
    /// Daemon::stop() before the owning unique_ptr members destroy the
    /// backing objects — after detach() the adaptor's D-Bus slots hit the
    /// null-guard paths instead of dereferencing a dangling pointer, and
    /// the destructor is a no-op for the save-on-teardown branch.
    void detach();

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
     * containing only the keys that were found. An unknown key is omitted from the
     * result and logged at DEBUG level, so a caller probing for optional settings does
     * not spam production logs. Callers fall back to their own defaults for anything
     * missing.
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
     * Applies all values via the setter registry, saves once (synchronously), and lets
     * the config backend's change notification propagate settingsChanged.
     *
     * An unknown key makes the call return false — writing a key nobody knows is a caller
     * bug, not an optional probe — but it does not abort the batch: every key that IS known
     * still gets applied. It is logged at DEBUG, not warning: the key comes from whoever is
     * on the session bus, and a warning let any process fill the daemon's log with content
     * of its own choosing. The `false` is the real signal.
     *
     * @param settings Map of setting key -> value
     * @return true if every key was known to the registry and every key that HAS a setter
     *         applied successfully. A key that is registered read-only (motionProfileTree,
     *         animationShaderSearchPaths, the gap projections) is skipped without failing
     *         the batch — see the skip's rationale at the call site. Only an UNKNOWN key
     *         makes this false.
     */
    bool setSettings(const QVariantMap& settings);

    QStringList getSettingKeys();

    // Per-screen settings (categories: "autotile", "snapping", "zoneSelector")
    void setPerScreenSetting(const QString& screenId, const QString& category, const QString& key,
                             const QDBusVariant& value);
    void clearPerScreenSettings(const QString& screenId, const QString& category);
    QVariantMap getPerScreenSettings(const QString& screenId, const QString& category);

    /**
     * @brief Batch-set multiple per-screen keys in one D-Bus call.
     *
     * Applies every (key, value) pair in @p values via the same category
     * dispatch as setPerScreenSetting, then schedules a single debounced
     * save. Mirrors the global getSettings/setSettings batch pattern.
     *
     * NOTE: nothing in this tree calls it. It was added for a KCM per-monitor flush that
     * never materialised — the KCM writes config.json in-process and calls
     * reloadSettings(). It stays as part of the PUBLISHED D-Bus write surface (declared in
     * org.plasmazones.Settings.xml, covered by tests/unit/dbus), for external clients. Do
     * not re-justify it by naming an in-tree caller; there is none.
     *
     * Unknown keys inside @p values are passed through to the underlying
     * Settings method, which no-ops and logs at DEBUG (the key is caller-supplied; see
     * setSettings) — consistent with single-key behavior.
     *
     * @param screenId Virtual or physical screen identifier
     * @param category "autotile" | "snapping" | "zoneSelector". NOTE: "snapping" is
     *                 READ-ONLY (it projects the config's per-monitor gaps), so a write to
     *                 it is rejected and does nothing. Write those through "autotile".
     * @param values   Map of key -> value. QDBusArgument-wrapped values
     *                 from the wire are unwrapped via DBusVariantUtils
     *                 before reaching the setter.
     * @return true if the category was recognized and writable, and the batch ran; false if
     *         the category was unknown, or is the read-only "snapping" projection. (There is
     *         no longer any concrete-backend check — the dispatch runs against ISettings.)
     *         Per-key failures are logged by the underlying Settings but
     *         cannot be surfaced here since the setters return void.
     */
    bool setPerScreenSettings(const QString& screenId, const QString& category, const QVariantMap& values);

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
     * @return Path to ~/.local/share/plasmazones/overlays
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
     * @brief Asynchronous window list request (fire-and-forget).
     *
     * Emits runningWindowsRequested() so the KWin effect enumerates its
     * window list and sends it back via provideRunningWindows(). The
     * resulting JSON is then broadcast via the runningWindowsAvailable
     * signal — subscribers update their view when the signal arrives
     * rather than blocking on the D-Bus call. This is the only
     * supported path; the old blocking getRunningWindows() method was
     * removed in Phase 6 of refactor/dbus-performance.
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
     * @brief Daemon → KWin effect: the per-event motion-profile tree changed.
     *
     * Emitted when the motion-profile registry mutates (a per-event
     * `profiles/<path>.json` override was edited and the daemon
     * rescanned it).
     * Deliberately separate from @ref settingsChanged: the Settings app
     * listens only to settingsChanged, so routing registry mutations
     * here keeps the app's value-change / save-discard detection from
     * being reset by its own immediate profile-file writes. The kwin
     * effect subscribes to this signal and re-fetches `motionProfileTree`.
     */
    void motionProfileTreeChanged();

    /**
     * @brief Daemon → KWin effect: the session went idle, or came back.
     *
     * The effect pauses decoration-chain animation while idle. That is the only
     * thing that lets the GPU leave its top performance state: an animated pack
     * repaints every window carrying it on every vsync, and it is the existence of
     * per-frame work — not its size — that holds the clocks up, so making each
     * frame cheaper cannot recover the idle power. Not drawing can.
     *
     * The daemon owns the detection because idleness is a Wayland CLIENT concern
     * (`ext-idle-notify-v1`, via PhosphorServiceIdle) and the effect lives inside
     * the compositor, which serves that protocol rather than consuming it. The
     * effect only ever sees the resolved boolean.
     */
    void sessionIdleChanged(bool idle);

    /**
     * @brief Daemon → KWin effect: please enumerate running windows.
     *
     * Emitted by requestRunningWindows(). The effect answers by calling
     * provideRunningWindows(), which then broadcasts runningWindowsAvailable
     * to all subscribers.
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
    ShaderRegistry* m_shaderRegistry = nullptr; ///< Borrowed; outlives adaptor
    PhosphorAnimation::PhosphorProfileRegistry* m_profileRegistry = nullptr; ///< Borrowed; outlives adaptor

    // Registry pattern
    using Getter = std::function<QVariant()>;
    using Setter = std::function<bool(const QVariant&)>;

    QHash<QString, Getter> m_getters;
    QHash<QString, Setter> m_setters;
    QHash<QString, QString> m_schemas; // key -> type ("bool"|"int"|"double"|"string"|"color"|"stringlist"|"map")

    // Debounced save timer (performance optimization)
    QTimer* m_saveTimer = nullptr;
    static constexpr int SaveDebounceMs = 500; // 500ms debounce

    // Debounced motion-profile-tree change notifier. A single
    // ProfileLoader rescan calls reloadFromOwner(), which emits one
    // profileChanged per touched path plus a closing ownerReloaded —
    // N+1 registry signals for one logical change. Without coalescing,
    // each would emit motionProfileTreeChanged() and the kwin-effect
    // would re-fetch + re-parse the whole tree N+1 times. This timer
    // collapses the burst into a single emission.
    QTimer* m_motionTreeNotifyTimer = nullptr;
    static constexpr int MotionTreeNotifyDebounceMs = 50;

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
