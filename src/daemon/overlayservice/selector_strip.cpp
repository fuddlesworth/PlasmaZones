// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Strip-mode arm of the zone selector (scrolling screens whose engine
// provides the drag-insert selector): the popup model built from the
// injected strip-cards provider, and the hit-test that classifies the
// cursor into a DragInsertTarget mirror. Split out of selector.cpp /
// overlayservice.cpp by concern (both sit near their size ceilings) and
// because everything here is dead weight on snap/autotile screens.

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "core/types/stripselectorhittest.h"

#include <QQuickItem>
#include <QVector>

namespace PlasmaZones {

QVariantList OverlayService::buildStripList(const QString& screenId) const
{
    if (!m_stripCardsProvider || !isStripSelectorScreen(screenId)) {
        return {};
    }
    return m_stripCardsProvider(screenId, m_activeDragWindowId);
}

int OverlayService::visibleStripCardCount(const QString& screenId) const
{
    return buildStripList(screenId).size();
}

void OverlayService::refreshStripSelector(const QString& screenId)
{
    if (!m_zoneSelectorVisible) {
        return;
    }
    const auto it = m_screenStates.constFind(screenId);
    if (it == m_screenStates.constEnd() || !it->zoneSelectorSlot()) {
        return;
    }
    // Full property re-push: the card list, the bar geometry sized off the
    // new card count, and the scroll math all shift together when the strip
    // gains or loses the drag window's column at a preview boundary.
    updateZoneSelectorWindow(screenId);
    // The rebuilt cards renumber targets, so the stored selection is stale
    // by construction. The next cursor tick re-selects under the new list.
    m_selectedStripTarget = {};
    m_selectedStripScreenId.clear();
    auto* slot = it->zoneSelectorSlot();
    writeQmlProperty(slot, QStringLiteral("selectedStripColumn"), -1);
    writeQmlProperty(slot, QStringLiteral("selectedStripGap"), -1);
    writeQmlProperty(slot, QStringLiteral("selectedStripHalf"), -1);
}

void OverlayService::updateStripSelectorHit(QQuickItem* slot, int localX, int localY, const QString& screenId)
{
    const QVariantList stripColumns = slot->property("stripColumns").toList();
    const QPointF pos(localX, localY);

    StripSelectorHit hit;
    if (stripColumns.isEmpty()) {
        // Empty strip: the whole bar is one "open the first column" target.
        // The strip layer fills the bar's content area and renders the
        // empty-state hint, so its rect is the honest hit region.
        if (auto* stripLayer = findQmlItemByName(slot, QStringLiteral("zoneSelectorStripLayer"))) {
            if (mapVisibleRectToItem(stripLayer, slot).contains(pos)) {
                hit.gapIndex = 0;
            }
        }
    } else {
        // Rendered card rects keyed by delegate index — same read-back
        // discipline as the layout-mode walk in selector.cpp (QML layout is
        // the authority; the ScrollView clips scrolled-out cards, and an
        // un-laid-out delegate must be unhittable rather than guessed at).
        QVector<QRectF> cardRects(stripColumns.size());
        QVector<bool> tabbed(stripColumns.size(), false);
        for (int i = 0; i < stripColumns.size(); ++i) {
            tabbed[i] = stripColumns.at(i).toMap().value(QStringLiteral("tabbed")).toBool();
        }
        QVector<QQuickItem*> cardItems;
        collectQmlItemsByName(slot, QStringLiteral("stripColumnCard"), cardItems);
        for (QQuickItem* card : std::as_const(cardItems)) {
            bool indexOk = false;
            const int cardIndex = card->property("index").toInt(&indexOk);
            if (!indexOk || cardIndex < 0 || cardIndex >= cardRects.size()) {
                continue;
            }
            cardRects[cardIndex] = mapVisibleRectToItem(card, slot);
        }
        const qreal spacing = std::max(4, slot->property("indicatorSpacing").toInt());
        hit = classifyStripSelectorPoint(cardRects, tabbed, pos, spacing * 0.75);
    }

    SelectorStripTarget target;
    int qmlColumn = -1;
    int qmlGap = -1;
    int qmlHalf = -1;
    if (hit.gapIndex >= 0) {
        target.columnIndex = hit.gapIndex;
        target.newColumn = true;
        qmlGap = hit.gapIndex;
    } else if (hit.columnIndex >= 0) {
        target.columnIndex = hit.columnIndex;
        // Top half prepends (tile 0); bottom half and the tabbed whole-card
        // dock both append (-1) — appending to a Tabbed column IS the tab
        // dock, there is no separate verb.
        target.tileIndex = hit.half == 0 ? 0 : -1;
        qmlColumn = hit.columnIndex;
        qmlHalf = hit.half;
    }

    const bool changed = target.columnIndex != m_selectedStripTarget.columnIndex
        || target.tileIndex != m_selectedStripTarget.tileIndex || target.newColumn != m_selectedStripTarget.newColumn
        || (target.isValid() && m_selectedStripScreenId != screenId);
    if (!changed) {
        return;
    }
    m_selectedStripTarget = target;
    m_selectedStripScreenId = target.isValid() ? screenId : QString();
    // Exactly one selection family at a time: a strip write invalidates any
    // zone selection a cross-screen hop may have left behind.
    m_selectedLayoutId.clear();
    m_selectedZoneIndex = -1;
    m_selectedZoneRelGeo = QRectF();
    writeQmlProperty(slot, QStringLiteral("selectedStripColumn"), qmlColumn);
    writeQmlProperty(slot, QStringLiteral("selectedStripGap"), qmlGap);
    writeQmlProperty(slot, QStringLiteral("selectedStripHalf"), qmlHalf);
}

} // namespace PlasmaZones
