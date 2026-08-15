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
#include "common/stripcardserialize.h"
#include "core/platform/logging.h"
#include "core/types/stripselectorhittest.h"

#include <QQuickItem>
#include <QVector>

#include <algorithm>

namespace PlasmaZones {

QVariantList OverlayService::buildStripList(const QString& screenId) const
{
    if (!m_stripCardsProvider || !isStripSelectorScreen(screenId)) {
        return {};
    }
    return m_stripCardsProvider(screenId, m_activeDragWindowId);
}

QList<qreal> OverlayService::stripCardFractions(const QString& screenId) const
{
    // Memoized: the trigger-edge probe asks per cursor tick, and the answer
    // only changes at the invalidation points listed on the cache member.
    // Extracting through buildStripList (not a separate engine query) keeps
    // the row-for-row agreement with the rendered card list.
    const auto cached = m_stripCardFractionsCache.constFind(screenId);
    if (cached != m_stripCardFractionsCache.constEnd()) {
        return cached.value();
    }
    const QList<qreal> fractions = stripFractionsFromColumns(buildStripList(screenId));
    m_stripCardFractionsCache.insert(screenId, fractions);
    return fractions;
}

int OverlayService::visibleStripCardCount(const QString& screenId) const
{
    return static_cast<int>(stripCardFractions(screenId).size());
}

QList<qreal> OverlayService::selectorStripFractions(const QString& screenId) const
{
    // Non-strip screens answer empty (uniform layout cells need no
    // fractions); an empty strip is empty too — the caller's single
    // hittable cell comes from selectorCardCount's floor.
    if (!isStripSelectorScreen(screenId)) {
        return {};
    }
    return stripCardFractions(screenId);
}

void OverlayService::refreshStripSelector(const QString& screenId)
{
    // The reshaped strip renumbers targets, so a stored selection keyed to
    // THIS screen is stale by construction and is dropped even when the popup
    // is hidden — a hidden popup gets no hit-test updates, so nothing else
    // would ever invalidate it, and the settle path would still prefer it at
    // drop. Selections keyed to a different screen describe a strip this
    // reshape did not touch and survive. The next cursor tick re-selects
    // under the new list.
    if (PhosphorScreens::ScreenIdentity::screensMatch(m_selectedStripScreenId, screenId)) {
        m_selectedStripTarget = {};
        m_selectedStripScreenId.clear();
    }
    // The strip changed shape, so any memoized card count for it is stale.
    m_stripCardFractionsCache.remove(screenId);
    if (!m_zoneSelectorVisible) {
        return;
    }
    // isVisible, not just slot existence (slots are created once and only
    // setVisible(false) on hide): a screen whose slot a sibling modal has
    // temporarily hidden should not pay the engine snapshot + serialization
    // below per placementChanged — the restore path re-pushes the model via
    // showZoneSelectorSlotOnScreen -> updateZoneSelectorWindow anyway.
    if (const auto preIt = m_screenStates.constFind(screenId);
        preIt == m_screenStates.constEnd() || !preIt->zoneSelectorSlot() || !preIt->zoneSelectorSlot()->isVisible()) {
        return;
    }
    // Full property re-push: the card list, the bar geometry sized off the
    // new card count, and the scroll math all shift together when the strip
    // gains or loses the drag window's column at a preview boundary.
    updateZoneSelectorWindow(screenId);
    // Re-find after the update: updateZoneSelectorWindow writes the screen
    // state through non-const operator[], so an iterator held across it is
    // the detach hazard hideZoneSelector's comment warns about.
    const auto it = m_screenStates.constFind(screenId);
    if (it == m_screenStates.constEnd() || !it->zoneSelectorSlot()) {
        return;
    }
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
        // A refresh that just re-pushed stripColumns only SCHEDULED the Row's
        // layout pass (polish), and on the preview-begin tick this hit-test
        // runs in the same D-Bus turn with no frame in between — the card x
        // positions read back below would be one layout pass stale. Deliver
        // the pending polish synchronously so the rects describe the list
        // just handed to QML rather than the previous generation. Flushing
        // the ROW covers the dominant term — it is the positioner that
        // places the cards, and the items the refresh also polishes (slot /
        // grid / shader anchor) are plain Items with a no-op updatePolish.
        // Residual ancestor staleness (e.g. a ScrollView control relayout
        // still pending) self-heals on the next cursor tick, as before this
        // flush existed. The empty-strip arm above needs no flush — the
        // strip layer is anchors.fill.
        if (auto* stripRow = findQmlItemByName(slot, QStringLiteral("zoneSelectorStripRow"))) {
            stripRow->ensurePolished();
        }
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
        // The row is laid out along the strip's axis (ZoneSelectorContent's
        // Grid), so the classification has to be told the same thing. These
        // two MUST agree: a horizontal classification over a vertical row
        // reverses which half of a card means "first tile" versus "append",
        // silently and with no visual tell.
        hit = classifyStripSelectorPoint(cardRects, tabbed, pos, spacing * 0.75, stripIsVertical(screenId));
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

    // Exactly one selection family at a time: entering the strip hit-test at
    // all means the cursor is on a strip screen, so any zone selection a
    // cross-screen hop left behind is stale even when THIS strip's hit is
    // unchanged (invalid -> invalid included) — clearing it must not hide
    // behind the changed-gate below.
    m_selectedLayoutId.clear();
    m_selectedZoneIndex = -1;
    m_selectedZoneRelGeo = QRectF();

    const bool changed = target.columnIndex != m_selectedStripTarget.columnIndex
        || target.tileIndex != m_selectedStripTarget.tileIndex || target.newColumn != m_selectedStripTarget.newColumn
        || (target.isValid() && !PhosphorScreens::ScreenIdentity::screensMatch(m_selectedStripScreenId, screenId));
    if (!changed) {
        return;
    }
    m_selectedStripTarget = target;
    m_selectedStripScreenId = target.isValid() ? screenId : QString();
    writeQmlProperty(slot, QStringLiteral("selectedStripColumn"), qmlColumn);
    writeQmlProperty(slot, QStringLiteral("selectedStripGap"), qmlGap);
    writeQmlProperty(slot, QStringLiteral("selectedStripHalf"), qmlHalf);
}

} // namespace PlasmaZones
