// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlgorithmRegistry.h"
#include "TilingAlgorithm.h"
#include "TilingState.h"
#include "config/configdefaults.h"
#include "core/constants.h"
#include "core/layout.h"
#include "core/logging.h"
#include "pz_i18n.h"

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
    if (old && old != algorithm) {
        old->deleteLater();
    }

    // Take ownership
    algorithm->setParent(this);
    m_algorithms.insert(id, algorithm);
    if (oldIndex >= 0) {
        m_registrationOrder.insert(oldIndex, id);
    } else {
        m_registrationOrder.append(id);
    }

    Q_EMIT algorithmRegistered(id);
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

    algorithm->deleteLater();
    Q_EMIT algorithmUnregistered(id);
    return true;
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
    return ConfigDefaults::autotileAlgorithm();
}

TilingAlgorithm* AlgorithmRegistry::defaultAlgorithm() const
{
    return algorithm(defaultAlgorithmId());
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

namespace {
/**
 * @brief Check if all zones have identical geometry (monocle-style)
 *
 * Monocle algorithm returns all windows at fullscreen, which would cause
 * zone numbers to stack on top of each other in preview. We detect this
 * and apply visual offsets.
 */
bool areAllZonesIdentical(const QVector<QRect>& zones)
{
    if (zones.size() <= 1) {
        return false; // Nothing to offset for single zone
    }

    const QRect& first = zones.first();
    for (int i = 1; i < zones.size(); ++i) {
        if (zones[i] != first) {
            return false;
        }
    }
    return true;
}
} // namespace

QVariantList AlgorithmRegistry::zonesToRelativeGeometry(const QVector<QRect>& zones, const QRect& previewRect)
{
    if (!previewRect.isValid() || previewRect.width() == 0 || previewRect.height() == 0) {
        return {};
    }

    QVariantList result;
    const bool isMonocle = areAllZonesIdentical(zones);

    for (int i = 0; i < zones.size(); ++i) {
        QVariantMap zoneMap;
        zoneMap[QLatin1String("zoneNumber")] = i + 1;

        QVariantMap relGeo;
        if (isMonocle) {
            const qreal offset = i * MonoclePreviewOffset;
            relGeo[QLatin1String("x")] = offset;
            relGeo[QLatin1String("y")] = offset;
            relGeo[QLatin1String("width")] = qMax(0.01, 1.0 - offset * 2);
            relGeo[QLatin1String("height")] = qMax(0.01, 1.0 - offset * 2);
        } else {
            relGeo[QLatin1String("x")] = static_cast<qreal>(zones[i].x()) / previewRect.width();
            relGeo[QLatin1String("y")] = static_cast<qreal>(zones[i].y()) / previewRect.height();
            relGeo[QLatin1String("width")] = static_cast<qreal>(zones[i].width()) / previewRect.width();
            relGeo[QLatin1String("height")] = static_cast<qreal>(zones[i].height()) / previewRect.height();
        }
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

    QVector<QRect> zones = algorithm->calculateZones({count, previewRect, &previewState, 0, {}});

    // Convert to relative geometry (handles monocle offset detection internally)
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

QVariantMap AlgorithmRegistry::algorithmToVariantMap(TilingAlgorithm* algorithm, const QString& algorithmId)
{
    QVariantMap map;

    if (!algorithm) {
        return map;
    }

    // Use autotile: prefix for ID to distinguish from manual layout UUIDs
    map[QLatin1String("id")] = LayoutId::makeAutotileId(algorithmId);
    map[QLatin1String("name")] = algorithm->name();
    map[QLatin1String("description")] = algorithm->description();
    map[QLatin1String("zoneCount")] = effectiveMaxWindows(algorithm);
    map[QLatin1String("zones")] = generatePreviewZones(algorithm);
    map[QLatin1String("category")] = static_cast<int>(LayoutCategory::Autotile);
    map[QLatin1String("zoneNumberDisplay")] = algorithm->zoneNumberDisplay();
    if (algorithm->supportsMemory()) {
        map[QLatin1String("memory")] = true;
    }

    // Section grouping for LayoutsPage
    const auto section = sectionForAlgorithm(algorithm);
    map[QLatin1String("sectionKey")] = section.key;
    map[QLatin1String("sectionLabel")] = section.label;
    map[QLatin1String("sectionOrder")] = section.order;

    return map;
}

AlgorithmRegistry::SectionInfo AlgorithmRegistry::sectionForAlgorithm(TilingAlgorithm* algorithm)
{
    if (!algorithm) {
        return {QStringLiteral("built-in"), PzI18n::tr("Built-in"), 0};
    }

    // User-created scripts get their own section so users can easily
    // distinguish their custom algorithms from bundled ones.
    if (algorithm->isScripted() && algorithm->isUserScript()) {
        return {QStringLiteral("custom"), PzI18n::tr("Custom"), 2};
    }

    // Memory-enabled algorithms (DwindleMemory, or system scripts with memory)
    // are grouped separately to highlight their persistent behavior.
    if (algorithm->supportsMemory()) {
        return {QStringLiteral("persistent"), PzI18n::tr("Persistent"), 1};
    }

    // Built-in C++ algorithms and system-installed scripts
    return {QStringLiteral("built-in"), PzI18n::tr("Built-in"), 0};
}

} // namespace PlasmaZones
