// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Snapping/tiling ordering helpers for SettingsController:
//   * effective* — current displayed order
//   * has*Custom — does the user have a non-default ordering?
//   * resolved*  — UI-facing list keyed by display label
//   * move*      — drag-to-reorder support
//   * reset*Order — wipe custom ordering, fall back to default
//
// Staged state lives in m_stagedSnappingOrder / m_stagedTilingOrder
// and is committed to Settings via the save() path in
// settingscontroller_lifecycle.cpp.
//
// Split out of settingscontroller_session.cpp to keep that file
// under the 800-line cap. Same class, separate TU, no API change.

#include "settingscontroller.h"

namespace PlasmaZones {

QStringList SettingsController::effectiveSnappingOrder() const
{
    return m_stagedSnappingOrder.value_or(m_settings.snappingLayoutOrder());
}

QStringList SettingsController::effectiveTilingOrder() const
{
    return m_stagedTilingOrder.value_or(m_settings.tilingAlgorithmOrder());
}

bool SettingsController::hasCustomSnappingOrder() const
{
    return !effectiveSnappingOrder().isEmpty();
}

bool SettingsController::hasCustomTilingOrder() const
{
    return !effectiveTilingOrder().isEmpty();
}

QVariantList SettingsController::resolvedSnappingOrder() const
{
    QHash<QString, QVariantMap> layoutMap;
    for (const QVariant& v : m_layouts) {
        QVariantMap map = v.toMap();
        QString id = map.value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && !map.value(QStringLiteral("isAutotile"), false).toBool()) {
            layoutMap.insert(id, map);
        }
    }
    return applyCustomOrder(effectiveSnappingOrder(), layoutMap);
}

QVariantList SettingsController::resolvedTilingOrder() const
{
    QHash<QString, QVariantMap> algoMap;
    for (const QVariant& v : availableAlgorithms()) {
        QVariantMap map = v.toMap();
        QString id = map.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            algoMap.insert(id, map);
        }
    }
    return applyCustomOrder(effectiveTilingOrder(), algoMap);
}

// Shared helper: move an item within a resolved order list and stage the result
static bool moveOrderedItem(const QVariantList& resolved, int fromIndex, int toIndex,
                            std::optional<QStringList>& staged)
{
    if (fromIndex < 0 || fromIndex >= resolved.size() || toIndex < 0 || toIndex >= resolved.size()
        || fromIndex == toIndex) {
        return false;
    }

    QStringList ids;
    ids.reserve(resolved.size());
    for (const QVariant& v : resolved) {
        ids.append(v.toMap().value(QStringLiteral("id")).toString());
    }
    ids.move(fromIndex, toIndex);
    staged = ids;
    return true;
}

void SettingsController::moveSnappingLayout(int fromIndex, int toIndex)
{
    if (moveOrderedItem(resolvedSnappingOrder(), fromIndex, toIndex, m_stagedSnappingOrder)) {
        Q_EMIT stagedSnappingOrderChanged();
        setNeedsSave(true);
    }
}

void SettingsController::moveTilingAlgorithm(int fromIndex, int toIndex)
{
    if (moveOrderedItem(resolvedTilingOrder(), fromIndex, toIndex, m_stagedTilingOrder)) {
        Q_EMIT stagedTilingOrderChanged();
        setNeedsSave(true);
    }
}

void SettingsController::resetSnappingOrder()
{
    m_stagedSnappingOrder = QStringList{};
    Q_EMIT stagedSnappingOrderChanged();
    setNeedsSave(true);
}

void SettingsController::resetTilingOrder()
{
    m_stagedTilingOrder = QStringList{};
    Q_EMIT stagedTilingOrderChanged();
    setNeedsSave(true);
}

} // namespace PlasmaZones
