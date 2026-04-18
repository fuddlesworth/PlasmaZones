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

namespace {
/// Library-owned recommended default algorithm.
/// Kept as a literal so the registry is self-contained — no PlasmaZones
/// config-layer dependency.  Application config layers may surface their own
/// user-facing default (which today happens to also be "bsp"); the two are
/// intentionally independent.
constexpr auto RecommendedDefaultAlgorithmId = "bsp";
}

namespace PhosphorTiles {

bool AlgorithmRegistry::PreviewParams::operator==(const PreviewParams& other) const
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
    : QObject(parent)
{
    registerBuiltInAlgorithms();

    // Connect cleanup to aboutToQuit — safe here because the constructor
    // runs exactly once under the C++11 static-init guarantee.
    if (QCoreApplication::instance()) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                         &AlgorithmRegistry::cleanup);
    }
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

AlgorithmRegistry* AlgorithmRegistry::instance()
{
    Q_ASSERT_X(QCoreApplication::instance(), "AlgorithmRegistry::instance",
               "must not be called before QCoreApplication is created");
    // Meyer's singleton: C++11 guarantees thread-safe initialization
    // of static local variables (§6.7 [stmt.dcl] p4).
    // The constructor connects cleanup() — no separate connection needed.
    static AlgorithmRegistry s_instance;
    return &s_instance;
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

QStringList AlgorithmRegistry::availableAlgorithms() const noexcept
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

bool AlgorithmRegistry::hasAlgorithm(const QString& id) const noexcept
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
    // Process all pending registrations from AlgorithmRegistrar instances
    // Each algorithm registers itself via static initialization in its .cpp file
    auto& pending = pendingAlgorithmRegistrations();

    // Sort by priority (lower = first) for deterministic registration order
    std::sort(pending.begin(), pending.end(), [](const auto& a, const auto& b) {
        return a.priority < b.priority;
    });

    for (const auto& reg : pending) {
        registerAlgorithm(reg.id, reg.factory());
    }

    // Clear pending list (registrations are now complete)
    pending.clear();
}

void AlgorithmRegistry::setPreviewParams(const PreviewParams& params)
{
    if (m_previewParams == params) {
        return;
    }
    m_previewParams = params;
    Q_EMIT previewParamsChanged();
}

const AlgorithmRegistry::PreviewParams& AlgorithmRegistry::previewParams() const noexcept
{
    return m_previewParams;
}

void AlgorithmRegistry::setConfiguredPreviewParams(const PreviewParams& params)
{
    instance()->setPreviewParams(params);
}

const AlgorithmRegistry::PreviewParams& AlgorithmRegistry::configuredPreviewParams()
{
    return instance()->previewParams();
}

} // namespace PhosphorTiles
