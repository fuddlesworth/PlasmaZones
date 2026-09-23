// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Snapping/tiling/scrolling ordering helpers for SettingsController:
//   * effective* — current displayed order
//   * has*Custom — does the user have a non-default ordering?
//   * resolved*  — UI-facing list keyed by display label
//   * move*      — drag-to-reorder support
//   * reset*Order — wipe custom ordering, fall back to default
//
// Staged state lives in m_stagedSnappingOrder / m_stagedTilingOrder /
// m_stagedScrollingOrder and is committed to Settings via the save()
// path in settingscontroller_lifecycle.cpp.
//
// Same class as settingscontroller_session.cpp, separate TU, no API change.

#include "settingscontroller.h"

#include "core/platform/logging.h"

#include <QDebug>
#include <QSet>
#include <QVector>

#include <algorithm>

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

    // Then: remaining items in default order (name-alphabetical). The label
    // key differs per family — layout/template rows from toVariantMap stamp
    // "displayName", algorithm rows from AlgorithmService stamp "name" — so
    // read whichever is present.
    const auto labelOf = [](const QVariantMap& map) {
        const QString displayName = map.value(QStringLiteral("displayName")).toString();
        return displayName.isEmpty() ? map.value(QStringLiteral("name")).toString() : displayName;
    };
    QVector<QPair<QString, QVariantMap>> remaining;
    for (auto it = itemMap.cbegin(); it != itemMap.cend(); ++it) {
        if (!added.contains(it.key())) {
            remaining.append({it.key(), it.value()});
        }
    }
    std::sort(remaining.begin(), remaining.end(), [&labelOf](const auto& a, const auto& b) {
        // Tie-break equal labels on the id (the map key): the vector is built
        // from QHash iteration, whose order is randomized per process, and
        // std::sort is unstable — without the tie-break two items sharing a
        // display name would swap positions between launches.
        const int cmp = labelOf(a.second).compare(labelOf(b.second), Qt::CaseInsensitive);
        if (cmp != 0)
            return cmp < 0;
        return a.first < b.first;
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
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            // A resolved-order entry missing its "id" key indicates an
            // upstream wiring bug (resolvedSnappingOrder / resolvedTilingOrder
            // should always stamp an id). Refuse the move rather than
            // staging a list that contains an empty-id slot — persisting
            // that would round-trip back as a phantom entry on next load.
            qCWarning(lcCore) << "moveOrderedItem: resolved entry at index" << ids.size()
                              << "has empty id; refusing to stage reorder";
            return false;
        }
        ids.append(id);
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

QStringList SettingsController::effectiveScrollingOrder() const
{
    return m_stagedScrollingOrder.value_or(m_settings.scrollingTemplateOrder());
}

bool SettingsController::hasCustomSnappingOrder() const
{
    return !effectiveSnappingOrder().isEmpty();
}

bool SettingsController::hasCustomTilingOrder() const
{
    return !effectiveTilingOrder().isEmpty();
}

bool SettingsController::hasCustomScrollingOrder() const
{
    return !effectiveScrollingOrder().isEmpty();
}

QVariantList SettingsController::resolvedSnappingOrder() const
{
    QHash<QString, QVariantMap> layoutMap;
    for (const QVariant& v : m_layouts) {
        QVariantMap map = v.toMap();
        QString id = map.value(QStringLiteral("id")).toString();
        // Template rows share m_layouts with the manual layouts and are not
        // autotile, so they must be excluded explicitly or they leak into the
        // snapping priority list (they have their own scrolling order).
        if (!id.isEmpty() && !map.value(QStringLiteral("isAutotile"), false).toBool()
            && !map.value(QStringLiteral("isScrollingTemplate"), false).toBool()) {
            layoutMap.insert(id, map);
        }
    }
    return applyCustomOrder(effectiveSnappingOrder(), layoutMap);
}

QVariantList SettingsController::resolvedScrollingOrder() const
{
    QHash<QString, QVariantMap> templateMap;
    for (const QVariant& v : m_layouts) {
        QVariantMap map = v.toMap();
        QString id = map.value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && map.value(QStringLiteral("isScrollingTemplate"), false).toBool()) {
            templateMap.insert(id, map);
        }
    }
    return applyCustomOrder(effectiveScrollingOrder(), templateMap);
}

QVariantList SettingsController::resolvedTilingOrder(bool stampPreviews) const
{
    QHash<QString, QVariantMap> algoMap;
    for (const QVariant& v : availableAlgorithms()) {
        QVariantMap map = v.toMap();
        QString id = map.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            // Stamp the preview here rather than in a QML loop over the
            // returned rows: assembling the row is controller business, and
            // doing it in one pass spares the page N invokable round-trips
            // per rebuild. Gated because it is EXPENSIVE — each stamp runs
            // the algorithm to lay out its preview zones — and the id-only
            // caller (moveTilingAlgorithm) must not pay it per arrow press.
            if (stampPreviews) {
                map.insert(QStringLiteral("previewZones"), generateAlgorithmDefaultPreview(id));
            }
            algoMap.insert(id, map);
        }
    }
    return applyCustomOrder(effectiveTilingOrder(), algoMap);
}

// The move* and reset*Order bodies below reconcile the ordering page's own
// dirty membership instead of calling setNeedsSave(true). setNeedsSave
// inserts the ACTIVE page unconditionally and nothing ever removes that
// entry when a later drag restores the effective order it started from —
// staging never touches Settings, so no NOTIFY-driven reconcile fires and
// the footer stays lit with no badge. reconcilePageDirty makes the mark
// value-based in BOTH directions against isPageDirty's ordering branch,
// which is exactly how discardPage's ordering arm already settles these
// pages. The hardcoded page id is correct for every caller: the QML
// ordering pages invoke their own family's helper, and resetPage's
// ordering dispatch routes each ordering page id to its matching reset.

void SettingsController::moveSnappingLayout(int fromIndex, int toIndex)
{
    if (moveOrderedItem(resolvedSnappingOrder(), fromIndex, toIndex, m_stagedSnappingOrder)) {
        Q_EMIT stagedSnappingOrderChanged();
        reconcilePageDirty(QStringLiteral("snapping-ordering"));
    }
}

void SettingsController::moveTilingAlgorithm(int fromIndex, int toIndex)
{
    if (moveOrderedItem(resolvedTilingOrder(/*stampPreviews=*/false), fromIndex, toIndex, m_stagedTilingOrder)) {
        Q_EMIT stagedTilingOrderChanged();
        reconcilePageDirty(QStringLiteral("tiling-ordering"));
    }
}

void SettingsController::moveScrollingTemplate(int fromIndex, int toIndex)
{
    if (moveOrderedItem(resolvedScrollingOrder(), fromIndex, toIndex, m_stagedScrollingOrder)) {
        Q_EMIT stagedScrollingOrderChanged();
        reconcilePageDirty(QStringLiteral("scrolling-ordering"));
    }
}

void SettingsController::resetSnappingOrder()
{
    // Skip the staged-empty write + NOTIFY + dirty-flag when the
    // effective order is ALREADY empty — there's no observable user-
    // facing change. Two cases:
    //   (a) staged optional is engaged-empty: a re-Reset is a no-op,
    //   (b) staged optional is nullopt AND settings-layer order is
    //       also empty: a Reset on a fresh page where the user
    //       never customised would otherwise stamp nullopt→{} into
    //       staged (which IS a state change for the optional) AND
    //       call setNeedsSave(true) — surfacing an unsaved-changes
    //       indicator with nothing actually unsaved.
    // Both cases short-circuit; only a Reset that ACTUALLY transitions
    // a non-empty effective order to an empty one falls through.
    const bool stagedEmpty = m_stagedSnappingOrder.has_value() && m_stagedSnappingOrder->isEmpty();
    const bool settingsEmpty = !m_stagedSnappingOrder.has_value() && m_settings.snappingLayoutOrder().isEmpty();
    if (stagedEmpty || settingsEmpty) {
        return;
    }
    m_stagedSnappingOrder = QStringList{};
    Q_EMIT stagedSnappingOrderChanged();
    reconcilePageDirty(QStringLiteral("snapping-ordering"));
}

void SettingsController::resetTilingOrder()
{
    // Same effective-order short-circuit as resetSnappingOrder above.
    const bool stagedEmpty = m_stagedTilingOrder.has_value() && m_stagedTilingOrder->isEmpty();
    const bool settingsEmpty = !m_stagedTilingOrder.has_value() && m_settings.tilingAlgorithmOrder().isEmpty();
    if (stagedEmpty || settingsEmpty) {
        return;
    }
    m_stagedTilingOrder = QStringList{};
    Q_EMIT stagedTilingOrderChanged();
    reconcilePageDirty(QStringLiteral("tiling-ordering"));
}

void SettingsController::resetScrollingOrder()
{
    // Same effective-order short-circuit as resetSnappingOrder above.
    const bool stagedEmpty = m_stagedScrollingOrder.has_value() && m_stagedScrollingOrder->isEmpty();
    const bool settingsEmpty = !m_stagedScrollingOrder.has_value() && m_settings.scrollingTemplateOrder().isEmpty();
    if (stagedEmpty || settingsEmpty) {
        return;
    }
    m_stagedScrollingOrder = QStringList{};
    Q_EMIT stagedScrollingOrderChanged();
    reconcilePageDirty(QStringLiteral("scrolling-ordering"));
}

} // namespace PlasmaZones
