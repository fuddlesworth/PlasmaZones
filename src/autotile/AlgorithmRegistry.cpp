// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlgorithmRegistry.h"
#include "TilingAlgorithm.h"
#include "TilingState.h"
#include "core/constants.h"
#include "core/layout.h"
#include "core/logging.h"

#include <QDebug>
#include <QRect>
#include <algorithm>

namespace {
/// Use 1000x1000 for high-precision relative coordinate conversion
constexpr int PreviewSize = 1000;
}

namespace PlasmaZones {

using namespace DBus::AutotileAlgorithm;

// Global pending registrations list - shared by all AlgorithmRegistrar instantiations
QList<PendingAlgorithmRegistration> &pendingAlgorithmRegistrations()
{
    static QList<PendingAlgorithmRegistration> s_pending;
    return s_pending;
}

AlgorithmRegistry::AlgorithmRegistry(QObject *parent)
    : QObject(parent)
{
    registerBuiltInAlgorithms();
}

AlgorithmRegistry::~AlgorithmRegistry()
{
    // Clear the hash first. The TilingAlgorithm objects are QObject children
    // of this registry, so they will be deleted by ~QObject() after this
    // destructor completes. We clear the hash to prevent any accidental
    // access to stale pointers during destruction.
    m_algorithms.clear();
    m_registrationOrder.clear();
}

AlgorithmRegistry *AlgorithmRegistry::instance()
{
    // Meyer's singleton: C++11 guarantees thread-safe initialization
    // of static local variables (§6.7 [stmt.dcl] p4)
    static AlgorithmRegistry s_instance;
    return &s_instance;
}

void AlgorithmRegistry::registerAlgorithm(const QString &id, TilingAlgorithm *algorithm)
{
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
        qWarning() << "AlgorithmRegistry: algorithm" << algorithm->name()
                   << "is already registered as" << existingId
                   << "- cannot register as" << id;
        // Note: NOT deleting because it's still registered under existingId
        return;
    }

    // Remove existing algorithm with same ID (replacement case)
    auto *old = removeAlgorithmInternal(id);
    if (old && old != algorithm) {
        delete old;
    }

    // Take ownership
    algorithm->setParent(this);
    m_algorithms.insert(id, algorithm);
    m_registrationOrder.append(id);

    Q_EMIT algorithmRegistered(id);
}

QString AlgorithmRegistry::findAlgorithmId(TilingAlgorithm *algorithm) const
{
    for (auto it = m_algorithms.constBegin(); it != m_algorithms.constEnd(); ++it) {
        if (it.value() == algorithm) {
            return it.key();
        }
    }
    return QString();
}

TilingAlgorithm *AlgorithmRegistry::removeAlgorithmInternal(const QString &id)
{
    if (!m_algorithms.contains(id)) {
        return nullptr;
    }
    auto *algorithm = m_algorithms.take(id);
    m_registrationOrder.removeOne(id);
    return algorithm;
}

bool AlgorithmRegistry::unregisterAlgorithm(const QString &id)
{
    auto *algorithm = removeAlgorithmInternal(id);
    if (!algorithm) {
        return false;
    }

    delete algorithm;
    Q_EMIT algorithmUnregistered(id);
    return true;
}

TilingAlgorithm *AlgorithmRegistry::algorithm(const QString &id) const
{
    return m_algorithms.value(id, nullptr);
}

QStringList AlgorithmRegistry::availableAlgorithms() const noexcept
{
    return m_registrationOrder;
}

QList<TilingAlgorithm *> AlgorithmRegistry::allAlgorithms() const
{
    QList<TilingAlgorithm *> result;
    result.reserve(m_registrationOrder.size());

    for (const QString &id : m_registrationOrder) {
        if (auto* algo = m_algorithms.value(id)) {
            result.append(algo);
        } else {
            qCWarning(lcAutotile) << "Algorithm ID in registration order not found in map:" << id
                                  << "- possible registration/unregistration bug";
        }
    }

    return result;
}

bool AlgorithmRegistry::hasAlgorithm(const QString &id) const noexcept
{
    return m_algorithms.contains(id);
}

QString AlgorithmRegistry::defaultAlgorithmId() noexcept
{
    return MasterStack;
}

TilingAlgorithm *AlgorithmRegistry::defaultAlgorithm() const
{
    return algorithm(defaultAlgorithmId());
}

void AlgorithmRegistry::registerBuiltInAlgorithms()
{
    // Process all pending registrations from AlgorithmRegistrar instances
    // Each algorithm registers itself via static initialization in its .cpp file
    auto &pending = pendingAlgorithmRegistrations();

    // Sort by priority (lower = first) for deterministic registration order
    std::sort(pending.begin(), pending.end(),
              [](const auto &a, const auto &b) { return a.priority < b.priority; });

    for (const auto &reg : pending) {
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
bool areAllZonesIdentical(const QVector<QRect> &zones)
{
    if (zones.size() <= 1) {
        return false; // Nothing to offset for single zone
    }

    const QRect &first = zones.first();
    for (int i = 1; i < zones.size(); ++i) {
        if (zones[i] != first) {
            return false;
        }
    }
    return true;
}
} // namespace

QVariantList AlgorithmRegistry::zonesToRelativeGeometry(const QVector<QRect> &zones, const QRect &previewRect)
{
    if (!previewRect.isValid() || previewRect.width() == 0 || previewRect.height() == 0) {
        return {};
    }

    QVariantList result;
    const bool isMonocle = areAllZonesIdentical(zones);

    for (int i = 0; i < zones.size(); ++i) {
        QVariantMap zoneMap;
        zoneMap[QStringLiteral("zoneNumber")] = i + 1;

        QVariantMap relGeo;
        if (isMonocle) {
            const qreal offset = i * MonoclePreviewOffset;
            relGeo[QStringLiteral("x")] = offset;
            relGeo[QStringLiteral("y")] = offset;
            relGeo[QStringLiteral("width")] = 1.0 - offset * 2;
            relGeo[QStringLiteral("height")] = 1.0 - offset * 2;
        } else {
            relGeo[QStringLiteral("x")] = static_cast<qreal>(zones[i].x()) / previewRect.width();
            relGeo[QStringLiteral("y")] = static_cast<qreal>(zones[i].y()) / previewRect.height();
            relGeo[QStringLiteral("width")] = static_cast<qreal>(zones[i].width()) / previewRect.width();
            relGeo[QStringLiteral("height")] = static_cast<qreal>(zones[i].height()) / previewRect.height();
        }
        zoneMap[QStringLiteral("relativeGeometry")] = relGeo;

        result.append(zoneMap);
    }

    return result;
}

QVariantList AlgorithmRegistry::generatePreviewZones(TilingAlgorithm *algorithm)
{
    if (!algorithm) {
        return {};
    }

    // Generate preview zones for a representative window count
    const QRect previewRect(0, 0, PreviewSize, PreviewSize);

    TilingState previewState(QStringLiteral("preview"));
    previewState.setMasterCount(1);
    previewState.setSplitRatio(AutotileDefaults::DefaultSplitRatio);

    QVector<QRect> zones = algorithm->calculateZones(algorithm->defaultMaxWindows(), previewRect, previewState);

    // Convert to relative geometry (handles monocle offset detection internally)
    QVariantList list = zonesToRelativeGeometry(zones, previewRect);

    // Enrich with extra fields needed by zone selector / layout cards
    for (int i = 0; i < list.size(); ++i) {
        QVariantMap zoneMap = list[i].toMap();
        zoneMap[QStringLiteral("id")] = QString::number(i);
        zoneMap[QStringLiteral("name")] = QString();
        zoneMap[QStringLiteral("useCustomColors")] = false;
        list[i] = zoneMap;
    }

    return list;
}

QVariantMap AlgorithmRegistry::algorithmToVariantMap(TilingAlgorithm *algorithm, const QString &algorithmId)
{
    QVariantMap map;

    if (!algorithm) {
        return map;
    }

    // Use autotile: prefix for ID to distinguish from manual layout UUIDs
    map[QStringLiteral("id")] = LayoutId::makeAutotileId(algorithmId);
    map[QStringLiteral("name")] = algorithm->name();
    map[QStringLiteral("description")] = algorithm->description();
    map[QStringLiteral("type")] = -1; // Not a standard LayoutType
    map[QStringLiteral("zoneCount")] = algorithm->defaultMaxWindows();
    map[QStringLiteral("zones")] = generatePreviewZones(algorithm);
    map[QStringLiteral("category")] = static_cast<int>(LayoutCategory::Autotile);

    return map;
}

} // namespace PlasmaZones