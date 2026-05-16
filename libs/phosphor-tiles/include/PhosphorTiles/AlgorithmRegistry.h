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
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPointer>
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
     * @brief Early cleanup of all registered algorithms.
     *
     * Wired to @c QCoreApplication::aboutToQuit so algorithm objects
     * (especially @c ScriptedAlgorithm instances with @c QJSEngine
     * internals) are destroyed while Qt is still fully alive, avoiding
     * teardown crashes if an instance outlives @c QCoreApplication.
     *
     * Drains exclusively this registry's own pending @c deleteLater()
     * algorithms (tracked in @c m_pendingDeletes), never the process-
     * wide DeferredDelete queue. This narrow scoping is load-bearing
     * after the singleton kill in PR #343: multiple registries
     * (daemon, editor, settings) each connect to @c aboutToQuit, and a
     * process-wide drain from one registry would force-delete pending
     * @c deleteLater() events for unrelated subsystems whose owners
     * have not yet run their teardown.
     *
     * Safe to call from the destructor as well — idempotent after the
     * first invocation.
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
     * @brief Get the library's recommended default algorithm ID.
     *
     * Owned by the algorithm layer itself (not the application config
     * layer) so the registry remains self-contained. Application config
     * layers may expose their own user-facing default; that is
     * intentionally independent.
     *
     * Returns the same id from any instance (type-level policy, not
     * per-instance state) — the static helper @c defaultAlgorithmId()
     * is still available for callers that don't hold a registry pointer.
     */
    QString defaultAlgorithmId() const override
    {
        return staticDefaultAlgorithmId();
    }

    /// Static accessor for the same id @c defaultAlgorithmId() returns.
    /// Provided for callers that already had a hard dependency on the
    /// concrete @c AlgorithmRegistry type.
    static QString staticDefaultAlgorithmId();

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

    /// Algorithms detached from the registry via @c safeDeleteAlgorithm
    /// and queued for deferred deletion. Tracked so @c cleanup can drain
    /// only these objects' pending @c QEvent::DeferredDelete events
    /// rather than the entire process-wide queue. @c QPointer auto-clears
    /// when Qt finally processes the deferred-delete event, so stale
    /// entries are harmless.
    QList<QPointer<TilingAlgorithm>> m_pendingDeletes;

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
 * @brief Global list of pending algorithm registrations.
 *
 * Separate from the template so every registrar instantiation appends
 * to the same list regardless of template specialisation.
 *
 * @warning Callers MUST hold @c pendingAlgorithmRegistrationsMutex()
 * across every access. The list is append-only at static-init time
 * and snapshot-only at @c AlgorithmRegistry construction, so contention
 * is rare — but with the singleton gone (PR #343) two registries can
 * be constructed concurrently in the same process (daemon + KCM tests),
 * and a Qt plugin loader spawning a worker thread that triggers
 * @c dlopen of an algorithm library is a realistic future case.
 */
PHOSPHORTILES_EXPORT QList<PendingAlgorithmRegistration>& pendingAlgorithmRegistrations();

/**
 * @brief Mutex protecting @c pendingAlgorithmRegistrations(). Same
 * Meyer's-singleton lifetime as the list itself.
 */
PHOSPHORTILES_EXPORT class QMutex& pendingAlgorithmRegistrationsMutex();

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
        // Store in the global (non-template) pending list. Locked so
        // multiple algorithm libraries' static-init can run concurrently
        // (Qt plugin loader on a worker thread, parallel test binaries
        // sharing a process) without corrupting the QList.
        QMutexLocker locker(&pendingAlgorithmRegistrationsMutex());
        pendingAlgorithmRegistrations().append({id, priority, []() {
                                                    return new T();
                                                }});
    }
};

} // namespace PhosphorTiles