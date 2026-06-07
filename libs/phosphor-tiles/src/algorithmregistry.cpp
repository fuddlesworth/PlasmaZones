// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include "tileslogging.h"

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/Registry.h>

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <memory>
#include <vector>

namespace {
/// Library-owned recommended default algorithm. Self-contained — no
/// Phosphor config-layer dependency. Application config layers may surface
/// their own user-facing default (today also "bsp"); the two are independent.
const QString& recommendedDefaultAlgorithmId()
{
    static const QString id = QStringLiteral("bsp");
    return id;
}
} // namespace

namespace PhosphorTiles {

// ═══════════════════════════════════════════════════════════════════════════════
// Decomposed storage: a PhosphorRegistry::Registry entry that OWNS one algorithm
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Wrap a freshly-constructed TilingAlgorithm in a shared_ptr whose deleter
/// DEFERS destruction via deleteLater() while Qt is alive. This is what makes
/// dropping an entry safe even though consumers may still hold a cached
/// algorithm pointer when the unregister signal fires — the object outlives the
/// current call stack and is destroyed on the next event-loop turn. Without an
/// event loop (unit tests with no QCoreApplication) it deletes directly, since
/// deleteLater would never be drained. Replaces the old setParent + manual
/// deleteLater + m_pendingDeletes bookkeeping.
std::shared_ptr<TilingAlgorithm> ownAlgorithm(TilingAlgorithm* raw)
{
    return std::shared_ptr<TilingAlgorithm>(raw, [](TilingAlgorithm* a) {
        if (!a) {
            return;
        }
        if (QCoreApplication::instance()) {
            a->deleteLater();
        } else {
            delete a;
        }
    });
}

/// Registry entry: a discovered/registered algorithm as a PhosphorRegistry
/// factory. The Registry<TileAlgorithmEntry> keys on id() (the algorithm's
/// registryId); displayName() is the algorithm's name. Owns the algorithm via
/// the deferred-delete shared_ptr above — so the registry IS the algorithm's
/// owner, retiring the old QObject-parent + safeDeleteAlgorithm scheme.
class TileAlgorithmEntry final : public PhosphorRegistry::IFactoryBase
{
public:
    explicit TileAlgorithmEntry(std::shared_ptr<TilingAlgorithm> algorithm)
        : m_algorithm(std::move(algorithm))
    {
    }
    [[nodiscard]] QString id() const override
    {
        return m_algorithm ? m_algorithm->registryId() : QString();
    }
    [[nodiscard]] QString displayName() const override
    {
        return m_algorithm ? m_algorithm->name() : QString();
    }
    [[nodiscard]] TilingAlgorithm* algorithm() const
    {
        return m_algorithm.get();
    }

private:
    std::shared_ptr<TilingAlgorithm> m_algorithm;
};

} // namespace

class AlgorithmRegistry::Impl
{
public:
    // The id-keyed catalogue (storage + lookup + thread-safety). The
    // ITileAlgorithmRegistry change signals are still emitted explicitly from
    // the mutation methods (the deliberate single-contentsChanged-per-mutation
    // design), so the registry's own notifier is intentionally not bridged.
    PhosphorRegistry::Registry<TileAlgorithmEntry> registry;
    // Preview-param config — NOT registry storage, just a sibling field.
    AlgorithmPreviewParams previewParams;
};

bool AlgorithmPreviewParams::operator==(const AlgorithmPreviewParams& other) const
{
    return algorithmId == other.algorithmId && maxWindows == other.maxWindows && masterCount == other.masterCount
        && qFuzzyCompare(1.0 + splitRatio, 1.0 + other.splitRatio)
        && savedAlgorithmSettings == other.savedAlgorithmSettings;
}

// Global pending registrations list — shared by all AlgorithmRegistrar
// instantiations. See header note: callers MUST hold the mutex.
QList<PendingAlgorithmRegistration>& pendingAlgorithmRegistrations()
{
    static QList<PendingAlgorithmRegistration> s_pending;
    return s_pending;
}

QMutex& pendingAlgorithmRegistrationsMutex()
{
    static QMutex s_mutex;
    return s_mutex;
}

AlgorithmRegistry::AlgorithmRegistry(QObject* parent)
    : ITileAlgorithmRegistry(parent)
    , m_impl(std::make_unique<Impl>())
{
    registerBuiltInAlgorithms();

    // Destroy the registry's algorithms (LuauTileAlgorithm lua_State internals)
    // while Qt is still alive. Safe to connect here — the ctor runs once.
    if (QCoreApplication::instance()) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                         &AlgorithmRegistry::cleanup);
    }

    // contentsChanged is emitted EXPLICITLY at the end of each mutation method
    // (registerAlgorithm, unregisterAlgorithm, setPreviewParams) — exactly one
    // emit per logical mutation, in the observable order (specific signal →
    // contentsChanged), with no throw-between-emit hazard. Consumers needing
    // discrimination connect to the specific signals directly.
}

AlgorithmRegistry::~AlgorithmRegistry()
{
    // Ensure algorithms are destroyed before ~QObject(). Normally cleanup()
    // already ran via aboutToQuit; guard the no-QCoreApplication case.
    cleanup();
}

void AlgorithmRegistry::cleanup()
{
    // Snapshot the owned algorithms, drop all entries (each schedules its
    // algorithm's deleteLater via the entry's deleter), then flush ONLY these
    // algorithms' deferred deletes so lua_State-holding ones die while Qt is
    // still alive — WITHOUT draining the process-wide DeferredDelete queue
    // (which would step on sibling registries' / subsystems' pending deletes,
    // the hazard the old per-object m_pendingDeletes drain guarded against).
    QList<TilingAlgorithm*> owned;
    m_impl->registry.forEach([&owned](const std::shared_ptr<TileAlgorithmEntry>& entry) {
        if (entry && entry->algorithm()) {
            owned.append(entry->algorithm());
        }
    });
    m_impl->registry.clear();
    if (QCoreApplication::instance()) {
        for (TilingAlgorithm* algo : std::as_const(owned)) {
            QCoreApplication::sendPostedEvents(algo, QEvent::DeferredDelete);
        }
    }
}

void AlgorithmRegistry::registerAlgorithm(const QString& id, TilingAlgorithm* algorithm)
{
    Q_ASSERT(!QCoreApplication::instance() || QThread::currentThread() == QCoreApplication::instance()->thread());

    if (!algorithm) {
        return;
    }
    // From here we own `algorithm`; delete it on every validation-failure path
    // to prevent leaks (matching the prior contract).
    if (id.isEmpty()) {
        delete algorithm;
        return;
    }
    // Reserved namespace: "autotile:" is the prefix LayoutId uses to wrap
    // algorithm ids into composite LayoutPreview ids.
    if (id.startsWith(QLatin1String("autotile:"))) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "AlgorithmRegistry: refusing algorithm id with reserved 'autotile:' prefix" << id;
        delete algorithm;
        return;
    }
    // IDs flow into LayoutPreview::id, JSON keys, D-Bus args, QML model roles.
    // Reject characters that would break a downstream parser. Allowed:
    // [A-Za-z0-9._:-] (':' enables "script:foo" namespacing).
    for (QChar c : id) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '-'
            || u == '_' || u == '.' || u == ':';
        if (!ok) {
            qCWarning(PhosphorTiles::lcTilesLib) << "AlgorithmRegistry: refusing algorithm id with invalid character"
                                                 << id << "(allowed: [A-Za-z0-9._:-])";
            delete algorithm;
            return;
        }
    }

    // Double-registration guard: the same pointer already registered under a
    // different id would be double-owned. Don't delete — it's still owned (and
    // will be freed) under its existing id.
    const QString existingId = algorithm->registryId();
    if (!existingId.isEmpty() && existingId != id) {
        const auto existing = m_impl->registry.factory(existingId);
        if (existing && existing->algorithm() == algorithm) {
            qCWarning(PhosphorTiles::lcTilesLib)
                << "AlgorithmRegistry: algorithm" << algorithm->name() << "is already registered as" << existingId
                << "- cannot register as" << id;
            return;
        }
    }

    const bool replacing = m_impl->registry.factory(id) != nullptr;
    algorithm->setRegistryId(id);
    // Replace: a prior entry at `id` is dropped here, deferring the old
    // algorithm's deletion past the signals below (the entry's deleter), with
    // the NEW algorithm already queryable when handlers run.
    m_impl->registry.registerFactory(std::make_shared<TileAlgorithmEntry>(ownAlgorithm(algorithm)), QString(),
                                     PhosphorRegistry::DuplicatePolicy::Replace);

    if (replacing) {
        Q_EMIT algorithmUnregistered(id, true);
        Q_EMIT algorithmRegistered(id);
    } else {
        Q_EMIT algorithmRegistered(id);
    }
    Q_EMIT contentsChanged();
}

bool AlgorithmRegistry::unregisterAlgorithm(const QString& id)
{
    Q_ASSERT(!QCoreApplication::instance() || QThread::currentThread() == QCoreApplication::instance()->thread());

    if (!m_impl->registry.unregisterFactory(id)) {
        return false;
    }
    // The entry was dropped above, deferring the algorithm's deletion (the
    // entry's deleteLater deleter) past these signals — so a handler holding a
    // cached algorithm pointer can still safely reference it; algorithm(id)
    // already returns nullptr.
    Q_EMIT algorithmUnregistered(id, false);
    Q_EMIT contentsChanged();
    return true;
}

TilingAlgorithm* AlgorithmRegistry::algorithm(const QString& id) const
{
    const auto entry = m_impl->registry.factory(id);
    return entry ? entry->algorithm() : nullptr;
}

QStringList AlgorithmRegistry::availableAlgorithms() const
{
    // Sorted for a stable cross-platform order (the registry stores in hash
    // order). The prior registration-order was a UI nicety, not a contract:
    // consumers iterate to build a preview list and tests assert membership /
    // count, never a specific order.
    QStringList ids = m_impl->registry.ids();
    std::sort(ids.begin(), ids.end());
    return ids;
}

QList<TilingAlgorithm*> AlgorithmRegistry::allAlgorithms() const
{
    const QStringList ids = availableAlgorithms();
    QList<TilingAlgorithm*> result;
    result.reserve(ids.size());
    for (const QString& id : ids) {
        if (auto* algo = algorithm(id)) {
            result.append(algo);
        }
    }
    return result;
}

bool AlgorithmRegistry::hasAlgorithm(const QString& id) const
{
    return m_impl->registry.factory(id) != nullptr;
}

QString AlgorithmRegistry::staticDefaultAlgorithmId()
{
    return recommendedDefaultAlgorithmId();
}

TilingAlgorithm* AlgorithmRegistry::defaultAlgorithm() const
{
    auto* algo = algorithm(staticDefaultAlgorithmId());
    if (!algo) {
        // Configured default not registered (e.g. BSP script failed to load).
        // Fall back to the first available (sorted) so callers never get
        // nullptr when algorithms exist.
        const QStringList ids = availableAlgorithms();
        if (!ids.isEmpty()) {
            algo = algorithm(ids.first());
        }
    }
    return algo;
}

void AlgorithmRegistry::registerBuiltInAlgorithms()
{
    // Snapshot the static-init registrations under the mutex (a concurrent
    // AlgorithmRegistrar ctor on a worker thread, or parallel test binaries,
    // could otherwise race the global list), then sort by priority (lower
    // first), tie-break on id for deterministic order across TUs. Each
    // registry constructs its own algorithm instances (factory returns a
    // fresh `new T()` per call), so multiple registries don't share objects.
    std::vector<PendingAlgorithmRegistration> snapshot;
    {
        QMutexLocker locker(&pendingAlgorithmRegistrationsMutex());
        const auto& pending = pendingAlgorithmRegistrations();
        snapshot.reserve(static_cast<std::size_t>(pending.size()));
        for (const auto& p : pending) {
            snapshot.push_back(p);
        }
    }

    std::sort(snapshot.begin(), snapshot.end(), [](const auto& a, const auto& b) {
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.id < b.id;
    });

    for (const auto& reg : snapshot) {
        registerAlgorithm(reg.id, reg.factory());
    }
}

void AlgorithmRegistry::setPreviewParams(const AlgorithmPreviewParams& params)
{
    if (m_impl->previewParams == params) {
        return;
    }
    m_impl->previewParams = params;
    Q_EMIT previewParamsChanged();
    Q_EMIT contentsChanged();
}

const AlgorithmPreviewParams& AlgorithmRegistry::previewParams() const noexcept
{
    return m_impl->previewParams;
}

} // namespace PhosphorTiles
