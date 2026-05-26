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

namespace {

// Apply custom order to a set of items, appending unordered items alphabetically.
// Anonymous namespace keeps this TU-local so the build doesn't depend on unity
// build merging this TU with sibling settingscontroller_*.cpp files.
QVariantList applyCustomOrder(const QStringList& customOrder, const QHash<QString, QVariantMap>& itemMap)
{
    QVariantList result;
    QSet<QString> added;

    // First: items in custom order (skip stale IDs)
    for (const QString& id : customOrder) {
        if (itemMap.contains(id)) {
            result.append(itemMap.value(id));
            added.insert(id);
        }
    }

    // Then: remaining items in default order (name-alphabetical)
    QVector<QPair<QString, QVariantMap>> remaining;
    for (auto it = itemMap.cbegin(); it != itemMap.cend(); ++it) {
        if (!added.contains(it.key())) {
            remaining.append({it.key(), it.value()});
        }
    }
    std::sort(remaining.begin(), remaining.end(), [](const auto& a, const auto& b) {
        return a.second.value(QStringLiteral("name"))
                   .toString()
                   .compare(b.second.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive)
            < 0;
    });
    for (const auto& pair : remaining) {
        result.append(pair.second);
    }

    return result;
}

// Move an item within a resolved order list and stage the result.
// Anonymous namespace for the same TU-locality reason as applyCustomOrder.
bool moveOrderedItem(const QVariantList& resolved, int fromIndex, int toIndex, std::optional<QStringList>& staged)
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

} // namespace

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
