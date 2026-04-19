// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include "tileslogging.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QThread>
#include <algorithm>
#include <vector>

namespace {
/// Library-owned recommended default algorithm.
/// Kept as a literal so the registry is self-contained — no PlasmaZones
/// config-layer dependency.  Application config layers may surface their own
/// user-facing default (which today happens to also be "bsp"); the two are
/// intentionally independent.
constexpr auto RecommendedDefaultAlgorithmId = "bsp";
}

namespace PhosphorTiles {

bool AlgorithmPreviewParams::operator==(const AlgorithmPreviewParams& other) const
{
    return algorithmId == other.algorithmId && maxWindows == other.maxWindows && masterCount == other.masterCount
        && qFuzzyCompare(1.0 + splitRatio, 1.0 + other.splitRatio)
        && savedAlgorithmSettings == other.savedAlgorithmSettings;
}

// Global pending registrations list - shared by all AlgorithmRegistrar instantiations
QList<PendingAlgorithmRegistration>& pendingAlgorithmRegistrations()
{
    static QList<PendingAlgorithmRegistration> s_pending;
    return s_pending;
}

AlgorithmRegistry::AlgorithmRegistry(QObject* parent)
    : ITileAlgorithmRegistry(parent)
{
    registerBuiltInAlgorithms();

    // Connect cleanup to aboutToQuit — safe here because the constructor
    // runs exactly once under the C++11 static-init guarantee.
    if (QCoreApplication::instance()) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                         &AlgorithmRegistry::cleanup);
    }

    // Bridge the three finer-grained mutation signals into the unified
    // ILayoutSourceRegistry::contentsChanged notifier. Any ILayoutSource
    // that self-wires contentsChanged (AutotileLayoutSource does) picks
    // up registry changes through this single connection instead of
    // hooking each specific signal. Consumers that need discrimination
    // still connect to the specific signals directly.
    //
    // A replacement pair emits algorithmUnregistered(id, replacing=true)
    // then algorithmRegistered(id) — a single logical mutation. Suppress
    // the contentsChanged bridge on the unregister half of the pair so
    // the replacement emits contentsChanged exactly once (the matching
    // registered signal fires it). Halves preview-cache rebuilds on
    // scripted-algorithm hot reload, where every replaced script used
    // to trigger two full rebuilds in every subscriber.
    connect(this, &ITileAlgorithmRegistry::algorithmRegistered, this, [this](const QString&) {
        Q_EMIT contentsChanged();
    });
    connect(this, &ITileAlgorithmRegistry::algorithmUnregistered, this, [this](const QString&, bool replacing) {
        if (!replacing) {
            Q_EMIT contentsChanged();
        }
    });
    connect(this, &ITileAlgorithmRegistry::previewParamsChanged, this, &ITileAlgorithmRegistry::contentsChanged);
}

AlgorithmRegistry::~AlgorithmRegistry()
{
    // Ensure algorithms are destroyed before ~QObject() runs.
    // Normally cleanup() has already been called via aboutToQuit(),
    // but guard against the case where it was not (e.g. no QCoreApplication).
    cleanup();
}

void AlgorithmRegistry::cleanup()
{
    if (m_algorithms.isEmpty()) {
        return; // Already cleaned up (e.g., aboutToQuit already ran)
    }

    // Drain any pending deleteLater() events posted by safeDeleteAlgorithm() so
    // those algorithms are destroyed now, while Qt is still fully alive.
    // Without this drain, a ScriptedAlgorithm queued via deleteLater() could be
    // destructed during static teardown after QCoreApplication has already
    // gone away, crashing in ~QJSEngine.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    // Explicitly delete all algorithm children while Qt is still alive.
    // This prevents crashes when the static singleton is destroyed after
    // QCoreApplication during static destruction (ScriptedAlgorithm
    // instances hold QJSEngine internals that require a live Qt runtime).
    // Use direct delete instead of deleteLater() — this runs during shutdown
    // where the event loop may not drain the deferred-delete queue before
    // ~QObject() tries to destroy remaining children (double-free risk).
    for (auto* algo : std::as_const(m_algorithms)) {
        algo->setParent(nullptr);
        delete algo;
    }
    m_algorithms.clear();
    m_registrationOrder.clear();
}

void AlgorithmRegistry::registerAlgorithm(const QString& id, TilingAlgorithm* algorithm)
{
    Q_ASSERT(!QCoreApplication::instance() || QThread::currentThread() == QCoreApplication::instance()->thread());

    // Validate inputs - take ownership and delete on failure to prevent leaks
    if (id.isEmpty()) {
        delete algorithm;
        return;
    }
    if (!algorithm) {
        return;
    }

    // Reserved namespace: "autotile:" is the prefix LayoutId uses to
    // wrap algorithm ids into composite LayoutPreview ids. An algorithm
    // self-naming with that prefix would round-trip as
    // "autotile:autotile:foo" — extractAlgorithmId still works, but the
    // doubled prefix is a foot-gun for anyone parsing ids by hand.
    if (id.startsWith(QLatin1String("autotile:"))) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "AlgorithmRegistry: refusing algorithm id with reserved 'autotile:' prefix" << id;
        delete algorithm;
        return;
    }
    // IDs flow into LayoutPreview::id ("autotile:<id>"), JSON keys, D-Bus
    // method arguments, and QML model roles. Reject characters that would
    // break any downstream parser before they reach those boundaries.
    // Allowed: ASCII letters, digits, '-', '_', '.', ':' (':' enables
    // namespacing like "script:foo" used by ScriptedAlgorithmLoader).
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

    // Check if this algorithm pointer is already registered under a different ID
    // to prevent double-free issues. Don't delete - it's still owned under the original ID.
    const QString existingId = algorithm->registryId();
    if (!existingId.isEmpty() && existingId != id && m_algorithms.value(existingId) == algorithm) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "AlgorithmRegistry: algorithm" << algorithm->name() << "is already registered as" << existingId
            << "- cannot register as" << id;
        // Note: NOT deleting because it's still registered under existingId
        return;
    }

    // Remove existing algorithm with same ID (replacement case)
    // Preserve registration order position so replacements don't shift to the end
    int oldIndex = m_registrationOrder.indexOf(id);
    auto* old = removeAlgorithmInternal(id);

    // Register the new algorithm BEFORE emitting signals so that signal handlers
    // querying the registry see the new algorithm already in place.
    algorithm->setParent(this);
    algorithm->setRegistryId(id);
    m_algorithms.insert(id, algorithm);
    if (oldIndex >= 0) {
        m_registrationOrder.insert(oldIndex, id);
    } else {
        m_registrationOrder.append(id);
    }

    if (old && old != algorithm) {
        // Emit unregistered then registered so listeners see the full replacement
        // sequence with the new algorithm already queryable.
        Q_EMIT algorithmUnregistered(id, true);
        Q_EMIT algorithmRegistered(id);

        safeDeleteAlgorithm(old);
    } else {
        Q_EMIT algorithmRegistered(id);
    }
}

TilingAlgorithm* AlgorithmRegistry::removeAlgorithmInternal(const QString& id)
{
    if (!m_algorithms.contains(id)) {
        return nullptr;
    }
    auto* algorithm = m_algorithms.take(id);
    m_registrationOrder.removeOne(id);
    return algorithm;
}

bool AlgorithmRegistry::unregisterAlgorithm(const QString& id)
{
    Q_ASSERT(!QCoreApplication::instance() || QThread::currentThread() == QCoreApplication::instance()->thread());

    auto* algorithm = removeAlgorithmInternal(id);
    if (!algorithm) {
        return false;
    }

    // Emit before delete so signal handlers can safely reference the id
    // without risk of use-after-free on any cached algorithm pointers.
    Q_EMIT algorithmUnregistered(id, false);

    safeDeleteAlgorithm(algorithm);
    return true;
}

void AlgorithmRegistry::safeDeleteAlgorithm(TilingAlgorithm* algo)
{
    if (!algo) {
        return;
    }
    // Use deleteLater() to avoid re-entrancy: signal handlers connected to
    // algorithmUnregistered may call back into the registry. Synchronous
    // delete during signal emission would risk use-after-free if a handler
    // holds a pointer to the algorithm.
    //
    // setParent(nullptr) detaches from the registry's QObject tree so the
    // registry destructor doesn't double-delete. This is safe because:
    //   1. The algorithm was already removed from m_algorithms by
    //      removeAlgorithmInternal(), so cleanup() won't see it.
    //   2. deleteLater() ensures the QObject event loop handles final deletion.
    algo->setRegistryId(QString());
    algo->setParent(nullptr);
    algo->deleteLater();
}

TilingAlgorithm* AlgorithmRegistry::algorithm(const QString& id) const
{
    return m_algorithms.value(id, nullptr);
}

QStringList AlgorithmRegistry::availableAlgorithms() const
{
    return m_registrationOrder;
}

QList<TilingAlgorithm*> AlgorithmRegistry::allAlgorithms() const
{
    QList<TilingAlgorithm*> result;
    result.reserve(m_registrationOrder.size());

    for (const QString& id : m_registrationOrder) {
        if (auto* algo = m_algorithms.value(id)) {
            result.append(algo);
        } else {
            qCWarning(PhosphorTiles::lcTilesLib) << "Algorithm ID in registration order not found in map:" << id
                                                 << "- possible registration/unregistration bug";
        }
    }

    return result;
}

bool AlgorithmRegistry::hasAlgorithm(const QString& id) const
{
    return m_algorithms.contains(id);
}

QString AlgorithmRegistry::defaultAlgorithmId()
{
    return QString::fromLatin1(RecommendedDefaultAlgorithmId);
}

TilingAlgorithm* AlgorithmRegistry::defaultAlgorithm() const
{
    auto* algo = algorithm(defaultAlgorithmId());
    if (!algo && !m_algorithms.isEmpty() && !m_registrationOrder.isEmpty()) {
        // Configured default not registered (e.g. BSP script failed to load).
        // Fall back to the first available algorithm so callers never get nullptr
        // when algorithms are registered.
        algo = m_algorithms.value(m_registrationOrder.first(), nullptr);
    }
    return algo;
}

void AlgorithmRegistry::registerBuiltInAlgorithms()
{
    // Process all pending registrations from AlgorithmRegistrar instances.
    // Each algorithm registers itself via static initialization in its
    // .cpp file. The pending list is the canonical snapshot of every
    // statically-known built-in; we iterate without draining so multiple
    // AlgorithmRegistry instances (daemon, editor, settings, tests) each
    // get their own freshly-constructed copy of every built-in. The
    // factory returns a `new T()` per call, so each registry owns its
    // own algorithm instances — no sharing.
    //
    // Snapshot into a local vector before sorting so concurrent registry
    // constructions (e.g. daemon + settings in the same test process)
    // don't race on the global list's underlying storage. The list is
    // append-only at static-init time (stable once main() runs), but
    // std::sort mutates container order — two constructors running on
    // different threads would otherwise trip over each other's swaps.
    std::vector<PendingAlgorithmRegistration> snapshot;
    {
        const auto& pending = pendingAlgorithmRegistrations();
        snapshot.reserve(static_cast<std::size_t>(pending.size()));
        for (const auto& p : pending) {
            snapshot.push_back(p);
        }
    }

    // Sort the local copy by priority (lower = first); tie-break on id for
    // deterministic registration order across translation units. Static-init
    // order across TUs is implementation-defined, so shared-priority
    // AlgorithmRegistrar instances would otherwise produce platform-
    // dependent registration order (and, with it, availableAlgorithms()
    // iteration and defaultAlgorithm() fallback selection).
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
    if (m_previewParams == params) {
        return;
    }
    m_previewParams = params;
    Q_EMIT previewParamsChanged();
}

const AlgorithmPreviewParams& AlgorithmRegistry::previewParams() const noexcept
{
    return m_previewParams;
}

} // namespace PhosphorTiles
