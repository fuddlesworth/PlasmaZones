// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>
#include "AutotileConstants.h"
#include <QHash>
#include <QLatin1String>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace PhosphorTiles {

class TilingAlgorithm;

/**
 * @brief Singleton registry for tiling algorithms
 *
 * AlgorithmRegistry provides factory access to all available tiling algorithms.
 * It manages algorithm lifecycle and provides discovery for UI components.
 *
 * Built-in algorithms are registered automatically on first access:
 * - BSP (default): Binary space partitioning
 * - Master-Stack: Classic master/stack layout
 * - Columns: Equal-width vertical columns
 *
 * All built-in algorithms are registered via @c builtinId JS scripts.
 *
 * Usage:
 * @code
 * auto *algo = AlgorithmRegistry::instance()->algorithm("master-stack");
 * if (algo) {
 *     auto zones = algo->calculateZones(windowCount, screenGeometry, state);
 * }
 * @endcode
 *
 * @note Thread Safety: The singleton instance() method uses Meyer's singleton
 *       pattern (C++11 static local), which is thread-safe. Read operations
 *       (algorithm(), availableAlgorithms(), hasAlgorithm()) are thread-safe.
 *       Registration/unregistration should only occur during initialization
 *       or from the main thread.
 *
 * @note Process scope: the singleton lives per-process. Daemon, editor, and
 *       settings each hold their own instance — registered algorithms and
 *       configured preview params do not cross process boundaries. Callers
 *       that need cross-process state (e.g. SettingsBridge seeding preview
 *       params on settings change) must invoke setConfiguredPreviewParams
 *       in each process that will render previews.
 *
 * @see TilingAlgorithm for the algorithm interface
 * @see AlgorithmRegistry::availableAlgorithms() for discovering algorithm IDs
 */
class PHOSPHORTILES_EXPORT AlgorithmRegistry : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AlgorithmRegistry)

public:
    /**
     * @brief Get the singleton instance
     *
     * Creates the registry and registers built-in algorithms on first call.
     *
     * @return Pointer to the global AlgorithmRegistry instance
     */
    static AlgorithmRegistry* instance();

    /**
     * @brief Early cleanup of all registered algorithms
     *
     * Connected to QCoreApplication::aboutToQuit() so that algorithm objects
     * (especially ScriptedAlgorithm instances with QJSEngine internals) are
     * destroyed while Qt is still fully alive, avoiding crashes during static
     * destruction when the singleton outlives QCoreApplication.
     */
    void cleanup();

    /**
     * @brief Register a tiling algorithm
     *
     * The registry takes ownership of the algorithm. If an algorithm with
     * the same ID already exists, the old one is deleted and replaced.
     *
     * @param id Unique identifier for the algorithm (e.g. QLatin1String("bsp"))
     * @param algorithm Algorithm instance (ownership transferred)
     */
    void registerAlgorithm(const QString& id, TilingAlgorithm* algorithm);

    /**
     * @brief Unregister and delete an algorithm
     *
     * @param id Algorithm ID to remove
     * @return true if algorithm was found and removed
     */
    bool unregisterAlgorithm(const QString& id);

    /**
     * @brief Get an algorithm by ID
     *
     * @param id Algorithm identifier
     * @return Pointer to algorithm, or nullptr if not found
     */
    TilingAlgorithm* algorithm(const QString& id) const;

    /**
     * @brief Get list of all registered algorithm IDs
     *
     * @return List of algorithm IDs in registration order
     */
    QStringList availableAlgorithms() const noexcept;

    /**
     * @brief Get all registered algorithm instances
     *
     * @return List of algorithm pointers (owned by registry)
     */
    QList<TilingAlgorithm*> allAlgorithms() const;

    /**
     * @brief Check if an algorithm is registered
     *
     * @param id Algorithm identifier
     * @return true if algorithm exists
     */
    bool hasAlgorithm(const QString& id) const noexcept;

    /**
     * @brief Get the library's recommended default algorithm ID
     *
     * Owned by the algorithm layer itself (not the application config layer)
     * so the registry remains self-contained.  Application config layers may
     * expose their own user-facing default; that is intentionally independent.
     */
    static QString defaultAlgorithmId();

    /**
     * @brief Get the default algorithm instance
     *
     * Convenience method equivalent to algorithm(defaultAlgorithmId())
     *
     * @return Pointer to default algorithm
     */
    TilingAlgorithm* defaultAlgorithm() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Preview utilities for unified layout model (shared by zone selector,
    // overlay service, daemon OSD, and KCM algorithm preview)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Canvas edge length used by every preview path that converts algorithm
    /// output into relative (0.0–1.0) geometry. Exposed publicly so external
    /// callers (e.g. @c AutotileLayoutSource, SettingsController's live-param
    /// preview) scale against the same canvas without drift.
    static constexpr int PreviewCanvasSize = 1000;

    /**
     * @brief Tiling parameters that affect algorithm preview generation
     */
    struct PreviewParams
    {
        QString algorithmId; ///< Active algorithm — maxWindows/splitRatio/masterCount apply only to this
        int maxWindows = -1; ///< -1 = use algorithm default
        int masterCount = -1; ///< -1 = use default (1)
        qreal splitRatio = -1.0; ///< -1 = use algorithm default

        /**
         * @brief Per-algorithm saved settings (masterCount, splitRatio)
         *
         * Generalised replacement for hard-coded centered-master fields.
         * Key = algorithm ID, value = QVariantMap with "masterCount" (int)
         * and "splitRatio" (qreal).
         */
        QHash<QString, QVariantMap> savedAlgorithmSettings;

        bool operator==(const PreviewParams& other) const;
        bool operator!=(const PreviewParams& other) const
        {
            return !(*this == other);
        }
    };

    /**
     * @brief Set the user-configured tiling parameters for preview generation
     *
     * Call this when the user's tiling settings change — the next
     * @c previewFromAlgorithm invocation will apply the updated master-count
     * / split-ratio / max-windows values for the active algorithm and consult
     * the per-algorithm saved entries for others. Emits @c previewParamsChanged
     * if the value differs from the currently configured one.
     *
     * Instance-owned state — each AlgorithmRegistry tracks its own preview
     * params. The static overloads below delegate to @c instance() for
     * backwards compatibility.
     */
    void setPreviewParams(const PreviewParams& params);

    /**
     * @brief Get the configured preview parameters for this registry
     */
    const PreviewParams& previewParams() const noexcept;

    /// Static backwards-compat shim — forwards to `instance()->setPreviewParams(...)`.
    static void setConfiguredPreviewParams(const PreviewParams& params);

    /// Static backwards-compat shim — forwards to `instance()->previewParams()`.
    static const PreviewParams& configuredPreviewParams();

Q_SIGNALS:
    /**
     * @brief Emitted when an algorithm is registered
     *
     * On replacement (re-registration of an existing ID),
     * algorithmUnregistered(id, true) is emitted first, then
     * algorithmRegistered(id). The new algorithm is already queryable
     * via algorithm(id) when either signal fires.
     *
     * @param id The registered algorithm's ID
     */
    void algorithmRegistered(const QString& id);

    /**
     * @brief Emitted when an algorithm is unregistered or replaced
     *
     * @param id The algorithm's ID
     * @param replacing true if a new algorithm has already been registered
     *        under @p id (replacement case). false if the algorithm was
     *        explicitly removed and @c algorithm(id) now returns nullptr.
     */
    void algorithmUnregistered(const QString& id, bool replacing);

    /**
     * @brief Emitted when the configured preview params change
     *
     * Fired from @c setConfiguredPreviewParams so preview caches
     * (AutotileLayoutSource) can invalidate and re-render with the new
     * master-count / split-ratio / per-algorithm saved values.
     */
    void previewParamsChanged();

private:
    explicit AlgorithmRegistry(QObject* parent = nullptr);
    ~AlgorithmRegistry() override;

    /**
     * @brief Register all built-in algorithms
     *
     * Called automatically during construction.
     */
    void registerBuiltInAlgorithms();

    /**
     * @brief Remove algorithm from internal data structures
     *
     * Does NOT delete the algorithm or emit signals. Used by both
     * registerAlgorithm (for replacement) and unregisterAlgorithm.
     *
     * @param id Algorithm ID to remove
     * @return Pointer to removed algorithm, or nullptr if not found
     */
    TilingAlgorithm* removeAlgorithmInternal(const QString& id);

    /**
     * @brief Safely delete an algorithm via deleteLater()
     *
     * Detaches the algorithm from parent ownership and schedules deferred
     * deletion to avoid re-entrancy issues during signal emission.
     *
     * @param algo Algorithm to delete (nullptr is a safe no-op)
     */
    void safeDeleteAlgorithm(TilingAlgorithm* algo);

    QHash<QString, TilingAlgorithm*> m_algorithms;
    QStringList m_registrationOrder; ///< Preserve order for UI

    PreviewParams m_previewParams; ///< User-configured tiling parameters for previews
};

/**
 * @brief Pending algorithm registration data
 */
struct PHOSPHORTILES_EXPORT PendingAlgorithmRegistration
{
    QString id;
    int priority;
    std::function<TilingAlgorithm*()> factory;
};

/**
 * @brief Global list of pending algorithm registrations
 *
 * This is separate from the template to ensure all registrations go to the
 * same list regardless of template instantiation.
 */
PHOSPHORTILES_EXPORT QList<PendingAlgorithmRegistration>& pendingAlgorithmRegistrations();

/**
 * @brief Helper for static self-registration of built-in algorithms
 *
 * Use this in algorithm .cpp files to register at static initialization time.
 * New algorithms can be added without
 * modifying AlgorithmRegistry.
 *
 * Usage in algorithm .cpp file:
 * @code
 * namespace {
 * PhosphorTiles::AlgorithmRegistrar<MyAlgorithm> registrar(
 *     QLatin1String("my-algo"), 10);  // priority 10
 * }
 * @endcode
 *
 * @tparam T Algorithm class (must inherit from TilingAlgorithm)
 */
template<typename T>
class AlgorithmRegistrar
{
public:
    /**
     * @brief Register an algorithm at static initialization time
     *
     * @param id Algorithm identifier (e.g. QLatin1String("master-stack"))
     * @param priority Registration order (lower = registered first, default 100)
     */
    explicit AlgorithmRegistrar(const QString& id, int priority = 100)
    {
        // Store in the global (non-template) pending list
        pendingAlgorithmRegistrations().append({id, priority, []() {
                                                    return new T();
                                                }});
    }
};

} // namespace PhosphorTiles