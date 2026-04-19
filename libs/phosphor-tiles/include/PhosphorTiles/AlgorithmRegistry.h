// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>
#include "AlgorithmPreviewParams.h"
#include "AutotileConstants.h"
#include "ITileAlgorithmRegistry.h"
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
 * @brief Concrete tiling-algorithm registry.
 *
 * Implements the @c ITileAlgorithmRegistry contract. Composition roots
 * (daemon, editor, settings, tests) construct their own instance and
 * inject it into every consumer that needs algorithm enumeration /
 * lookup / preview-param access. There is no process-global singleton —
 * plugin-based compositor/WM/shell deployment requires per-instance
 * ownership so plugins cannot corrupt each other's registry state.
 *
 * Built-in algorithms (BSP, Master-Stack, Columns) register
 * automatically in the constructor via the static
 * @c pendingAlgorithmRegistrations list populated by
 * @c AlgorithmRegistrar. Additional scripted algorithms are loaded by
 * @c ScriptedAlgorithmLoader against an injected registry.
 *
 * @note Thread Safety: read operations (@c algorithm, @c availableAlgorithms,
 *       @c hasAlgorithm) are thread-safe once construction has completed.
 *       Registration/unregistration should only occur from the thread
 *       that owns the registry (typically the main thread).
 *
 * @see TilingAlgorithm for the algorithm interface
 * @see ITileAlgorithmRegistry for the abstract contract
 */
class PHOSPHORTILES_EXPORT AlgorithmRegistry : public ITileAlgorithmRegistry
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AlgorithmRegistry)

public:
    /**
     * @brief Early cleanup of all registered algorithms
     *
     * Connected to QCoreApplication::aboutToQuit() so that algorithm objects
     * (especially ScriptedAlgorithm instances with QJSEngine internals) are
     * destroyed while Qt is still fully alive, avoiding crashes during
     * teardown if an instance outlives QCoreApplication.
     */
    void cleanup();

    // ─── ITileAlgorithmRegistry implementation ─────────────────────────────

    void registerAlgorithm(const QString& id, TilingAlgorithm* algorithm) override;
    bool unregisterAlgorithm(const QString& id) override;
    TilingAlgorithm* algorithm(const QString& id) const override;
    QStringList availableAlgorithms() const override;
    QList<TilingAlgorithm*> allAlgorithms() const override;
    bool hasAlgorithm(const QString& id) const override;
    TilingAlgorithm* defaultAlgorithm() const override;

    /**
     * @brief Get the library's recommended default algorithm ID
     *
     * Owned by the algorithm layer itself (not the application config layer)
     * so the registry remains self-contained.  Application config layers may
     * expose their own user-facing default; that is intentionally independent.
     *
     * Stays static — this is a type-level policy, not per-instance state.
     */
    static QString defaultAlgorithmId();

    // ═══════════════════════════════════════════════════════════════════════════
    // Preview utilities for unified layout model (shared by zone selector,
    // overlay service, daemon OSD, and KCM algorithm preview)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Canvas edge length used by every preview path that converts algorithm
    /// output into relative (0.0–1.0) geometry. Exposed publicly so external
    /// callers (e.g. @c AutotileLayoutSource, SettingsController's live-param
    /// preview) scale against the same canvas without drift.
    static constexpr int PreviewCanvasSize = 1000;

    // ─── Preview params (ITileAlgorithmRegistry overrides) ─────────────────

    void setPreviewParams(const AlgorithmPreviewParams& params) override;
    const AlgorithmPreviewParams& previewParams() const noexcept override;

    /// Composition roots (daemon, editor, settings, tests) construct
    /// their own registry instance. Built-in algorithms register
    /// automatically in the constructor.
    explicit AlgorithmRegistry(QObject* parent = nullptr);
    ~AlgorithmRegistry() override;

private:
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

    AlgorithmPreviewParams m_previewParams; ///< User-configured tiling parameters for previews
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