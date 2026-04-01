// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlgorithmRegistry.h"
#include "TilingAlgorithm.h"
#include "TilingState.h"
#include "config/configdefaults.h"
#include "core/constants.h"
#include "core/layout.h"
#include "core/logging.h"

#include <QCoreApplication>
#include <QDebug>
#include <QRect>
#include <QThread>
#include <algorithm>

namespace {
/// Use 1000x1000 for high-precision relative coordinate conversion
constexpr int PreviewSize = 1000;
}

namespace PlasmaZones {

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
    const QString existingId = findAlgorithmId(algorithm);
    if (!existingId.isEmpty() && existingId != id) {
        qCWarning(lcAutotile) << "AlgorithmRegistry: algorithm" << algorithm->name() << "is already registered as"
                              << existingId << "- cannot register as" << id;
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

QString AlgorithmRegistry::findAlgorithmId(TilingAlgorithm* algorithm) const
{
    for (auto it = m_algorithms.constBegin(); it != m_algorithms.constEnd(); ++it) {
        if (it.value() == algorithm) {
            return it.key();
        }
    }
    return QString();
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
            qCWarning(lcAutotile) << "Algorithm ID in registration order not found in map:" << id
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
    return ConfigDefaults::defaultAutotileAlgorithm();
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

QVariantList AlgorithmRegistry::zonesToRelativeGeometry(const QVector<QRect>& zones, const QRect& previewRect)
{
    if (!previewRect.isValid() || previewRect.width() == 0 || previewRect.height() == 0) {
        return {};
    }

    QVariantList result;
    for (int i = 0; i < zones.size(); ++i) {
        QVariantMap zoneMap;
        zoneMap[QLatin1String("zoneNumber")] = i + 1;

        QVariantMap relGeo;
        relGeo[QLatin1String("x")] = static_cast<qreal>(zones[i].x()) / previewRect.width();
        relGeo[QLatin1String("y")] = static_cast<qreal>(zones[i].y()) / previewRect.height();
        relGeo[QLatin1String("width")] = static_cast<qreal>(zones[i].width()) / previewRect.width();
        relGeo[QLatin1String("height")] = static_cast<qreal>(zones[i].height()) / previewRect.height();
        zoneMap[QLatin1String("relativeGeometry")] = relGeo;

        result.append(zoneMap);
    }

    return result;
}

void AlgorithmRegistry::setConfiguredPreviewParams(const PreviewParams& params)
{
    instance()->m_previewParams = params;
}

const AlgorithmRegistry::PreviewParams& AlgorithmRegistry::configuredPreviewParams()
{
    return instance()->m_previewParams;
}

int AlgorithmRegistry::configuredMaxWindows()
{
    return instance()->m_previewParams.maxWindows;
}

int AlgorithmRegistry::effectiveMaxWindows(TilingAlgorithm* algorithm)
{
    const auto& params = instance()->m_previewParams;
    if (!algorithm) {
        return params.maxWindows;
    }
    // Use the user-configured maxWindows only for the active algorithm.
    // Other algorithms use their own default so each preview is representative.
    if (params.maxWindows > 0 && !params.algorithmId.isEmpty()) {
        if (instance()->algorithm(params.algorithmId) == algorithm) {
            return params.maxWindows;
        }
    }
    return algorithm->defaultMaxWindows();
}

QVariantList AlgorithmRegistry::generatePreviewZones(TilingAlgorithm* algorithm, int windowCount)
{
    if (!algorithm) {
        return {};
    }

    // Use explicit override, then configured setting, then algorithm default
    int count = windowCount;
    if (count <= 0) {
        count = effectiveMaxWindows(algorithm);
    }

    // Apply configured params: active algorithm uses global masterCount/splitRatio,
    // other algorithms check savedAlgorithmSettings for per-algorithm overrides,
    // and fall back to their own defaults.
    auto* inst = instance();
    const auto& params = inst->m_previewParams;
    const bool isActive = !params.algorithmId.isEmpty() && inst->algorithm(params.algorithmId) == algorithm;

    int masterCount = 1;
    qreal splitRatio = algorithm->defaultSplitRatio();
    if (isActive) {
        if (params.masterCount > 0)
            masterCount = params.masterCount;
        if (params.splitRatio > 0)
            splitRatio = params.splitRatio;
    } else {
        // Look up per-algorithm saved settings from the generalized map
        const QString algoId = inst->findAlgorithmId(algorithm);
        const auto it = params.savedAlgorithmSettings.constFind(algoId);
        if (it != params.savedAlgorithmSettings.constEnd()) {
            const QVariantMap& saved = it.value();
            const int savedMasterCount = saved.value(PerAlgoKeys::MasterCount, -1).toInt();
            const qreal savedSplitRatio = saved.value(PerAlgoKeys::SplitRatio, -1.0).toDouble();
            if (savedMasterCount > 0)
                masterCount = savedMasterCount;
            if (savedSplitRatio > 0)
                splitRatio = savedSplitRatio;
        }
    }

    // Generate preview zones for a representative window count
    const QRect previewRect(0, 0, PreviewSize, PreviewSize);

    TilingState previewState(QStringLiteral("preview"));
    previewState.setMasterCount(masterCount);
    previewState.setSplitRatio(splitRatio);

    QVector<QRect> zones = algorithm->calculateZones(TilingParams::forPreview(count, previewRect, &previewState));

    QVariantList list = zonesToRelativeGeometry(zones, previewRect);

    // Enrich with extra fields needed by zone selector / layout cards
    for (int i = 0; i < list.size(); ++i) {
        QVariantMap zoneMap = list[i].toMap();
        zoneMap[QLatin1String("id")] = QString::number(i);
        zoneMap[QLatin1String("name")] = QString();
        zoneMap[QLatin1String("useCustomColors")] = false;
        list[i] = zoneMap;
    }

    return list;
}

} // namespace PlasmaZones
