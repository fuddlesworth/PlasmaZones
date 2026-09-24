// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>
#include "AlgorithmPreviewParams.h"
#include "AutotileConstants.h"
#include "ITileAlgorithmRegistry.h"
#include <QLatin1String>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>

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
 * The bundled algorithms (BSP, Master-Stack, Columns, …) ship as Luau scripts
 * in @c data/algorithms and are loaded by @c ScriptedAlgorithmLoader against an
 * injected registry, alongside user scripts. The @c AlgorithmRegistrar /
 * @c pendingAlgorithmRegistrations machinery remains as the extension point for
 * a future compiled-in C++ algorithm, but no in-tree algorithm currently uses
 * it.
 *
 * @note Thread Safety: the underlying id-keyed store (a thread-safe
 *       @c PhosphorRegistry::Registry) makes the read operations
 *       (@c algorithm, @c availableAlgorithms, @c hasAlgorithm) safe to call
 *       from any thread. Registration/unregistration must still occur on the
 *       thread that owns the registry (typically the main thread) — not
 *       because of the store, but because they drive @c deleteLater() and
 *       Qt signal emission, which are thread-affine.
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
     * (especially @c LuauTileAlgorithm instances with @c lua_State
     * internals) are destroyed while Qt is still fully alive, avoiding
     * teardown crashes if an instance outlives @c QCoreApplication.
     *
     * Drains exclusively this registry's own algorithms — it snapshots them
     * before @c clear()ing the registry (each entry's shared_ptr deleter then
     * schedules a @c deleteLater()) and calls
     * @c QCoreApplication::sendPostedEvents(algo, QEvent::DeferredDelete) per
     * algorithm, never the process-wide DeferredDelete queue. This narrow
     * scoping is load-bearing after the singleton kill in PR #343: multiple
     * registries (daemon, editor, settings) each connect to @c aboutToQuit,
     * and a process-wide drain from one registry would force-delete pending
     * @c deleteLater() events for unrelated subsystems whose owners have not
     * yet run their teardown.
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

    /// Storage + lifetime are now decomposed: the id-keyed catalogue is a
    /// PhosphorRegistry::Registry<TileAlgorithmEntry> (each entry owns its
    /// algorithm via a shared_ptr whose deleter defers to deleteLater while
    /// Qt is alive), and the preview-param config is a plain field — neither
    /// is registry storage. Held behind a pimpl so this header stays free of
    /// the registry/entry template + TilingAlgorithm ownership details.
    class Impl;
    std::unique_ptr<Impl> m_impl;
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