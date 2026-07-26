// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "scrollenginelogging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PhosphorScrollEngine {

namespace {

/// Distance a parked window sits beyond the screen edge. Small on purpose:
/// "just outside the nearest output" keeps coordinates sane for KWin and
/// gives scroll animations a believable enter/leave origin, and it is the
/// structural fix for the stuck-off-screen-window folklore (extreme
/// coordinates are never committed).
// All parked rects on a side share the same x (no per-distance spread);
// the margin just keeps them clear of edge-snap heuristics.
constexpr int kParkMargin = 16;

} // namespace

ScrollLayoutParams ScrollEngine::layoutParamsForScreen(const QString& screenId) const
{
    ScrollLayoutParams params;
    QRect area = m_screenManager ? m_screenManager->screenAvailableGeometry(screenId)
                                 : (m_availableGeometryProvider ? m_availableGeometryProvider(screenId) : QRect());
    int innerGap = 0;
    // The strip reads the shared Tiling.Gaps model through IScrollSettings'
    // forwarding accessors; outer gaps shrink the work area, the inner gap
    // separates columns and stacked tiles. Context gap rules (resolved by
    // the daemon-injected provider, PerScreenKeys-shaped) win per slot.
    int top = 0;
    int bottom = 0;
    int left = 0;
    int right = 0;
    if (auto* gaps = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings())) {
        innerGap = qMax(0, gaps->scrollingInnerGap());
        if (gaps->scrollingUsePerSideOuterGap()) {
            top = gaps->scrollingOuterGapTop();
            bottom = gaps->scrollingOuterGapBottom();
            left = gaps->scrollingOuterGapLeft();
            right = gaps->scrollingOuterGapRight();
        } else {
            top = bottom = left = right = gaps->scrollingOuterGap();
        }
    }
    if (m_contextGapProvider) {
        namespace PSK = PhosphorEngine::PerScreenKeys;
        const QVariantMap overrides = m_contextGapProvider(screenId);
        if (const auto it = overrides.constFind(PSK::InnerGap); it != overrides.constEnd()) {
            innerGap = qMax(0, it->toInt());
        }
        const bool perSide = overrides.value(PSK::UsePerSideOuterGap, false).toBool();
        if (const auto it = overrides.constFind(PSK::OuterGap); it != overrides.constEnd() && !perSide) {
            top = bottom = left = right = it->toInt();
        }
        if (perSide) {
            top = overrides.value(PSK::OuterGapTop, top).toInt();
            bottom = overrides.value(PSK::OuterGapBottom, bottom).toInt();
            left = overrides.value(PSK::OuterGapLeft, left).toInt();
            right = overrides.value(PSK::OuterGapRight, right).toInt();
        }
    }
    // Outer gaps must never invert the rect: an unknown/removed screen
    // yields a null area, and adjust() on it (or oversized gaps on a small
    // screen) would drive the width/height negative. Downstream consumers
    // only check isValid() on the apply path; strip math runs on every
    // params consumer, so clamp here.
    const QRect adjusted = area.adjusted(qMax(0, left), qMax(0, top), -qMax(0, right), -qMax(0, bottom));
    params.workArea = (adjusted.width() > 0 && adjusted.height() > 0) ? adjusted : QRect();
    params.gap = innerGap;
    params.presetColumnWidths = m_presetColumnWidths;
    params.presetWindowHeights = m_presetWindowHeights;
    params.centerFocusedColumn = effectiveCenterFocusedColumn(screenId);
    params.alwaysCenterSingleColumn = m_alwaysCenterSingleColumn;
    params.defaultColumnWidth = effectiveDefaultColumnWidth(screenId);
    return params;
}

void ScrollEngine::applyLayout(const QString& screenId, bool focusWindowAfter)
{
    ScrollState* state = stateForKey(currentKeyForScreen(screenId), false);
    if (!state) {
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    if (!params.workArea.isValid()) {
        // Screen went away or gaps swallowed it: the indicator must not
        // stay painted, and a scheduled-retile storm must not spam a
        // warning per tick (debug level; the first clear is the signal).
        clearTabStripsForScreen(screenId);
        qCDebug(lcScrollEngine) << "applyLayout: no valid work area for screen" << screenId;
        return;
    }
    // Re-apply the centering policy before resolving: a work-area change
    // (resolution, panels, outer gaps) or a centering-settings flip leaves
    // the stored anchor relative to the OLD width, and nothing else
    // re-derives it until the next focus move. Idempotent for a settled
    // strip (a fully-visible column stays put under Never/OnOverflow).
    state->strip().updateViewForFocus(params);
    const ResolvedStrip resolved = state->strip().relayout(params);
    if (resolved.columns.isEmpty()) {
        // The strip just emptied (last window closed / floated / released).
        // The tab-strip clear must still run — returning before it would
        // leave the indicator painted on an empty screen forever.
        clearTabStripsForScreen(screenId);
        return;
    }

    // Parking bounds come from the FULL screen geometry, not the work area —
    // a rect just outside the work area could still sit on-screen over a
    // panel. Fall back to the work area when the screen rect is unknown.
    QRect screenRect = m_screenManager ? m_screenManager->screenGeometry(screenId)
                                       : (m_screenGeometryProvider ? m_screenGeometryProvider(screenId) : QRect());
    if (!screenRect.isValid()) {
        screenRect = params.workArea;
    }

    QJsonArray arr;
    bool anyRectMoved = false;
    for (const ResolvedColumn& column : resolved.columns) {
        for (const ResolvedTile& tile : column.tiles) {
            QRect rect = tile.rect;
            if (tile.hidden) {
                // Non-active tile of a tabbed column: parked off-canvas so it
                // cannot steal input from the visible tab (hit-testing uses
                // real geometry only).
                rect.moveLeft(screenRect.right() + 1 + kParkMargin);
            } else if (rect.right() < params.workArea.left()) {
                rect.moveLeft(screenRect.left() - rect.width() - kParkMargin);
            } else if (rect.left() > params.workArea.right()) {
                rect.moveLeft(screenRect.right() + 1 + kParkMargin);
            }

            QJsonObject obj;
            obj[QLatin1String("windowId")] = tile.windowId;
            obj[QLatin1String("screenId")] = screenId;
            obj[QLatin1String("x")] = rect.x();
            obj[QLatin1String("y")] = rect.y();
            obj[QLatin1String("width")] = rect.width();
            obj[QLatin1String("height")] = rect.height();
            arr.append(obj);
            const auto lastIt = m_lastAppliedRect.constFind(tile.windowId);
            if (lastIt == m_lastAppliedRect.constEnd() || *lastIt != rect) {
                anyRectMoved = true;
            }
            m_lastAppliedRect.insert(tile.windowId, rect);
        }
    }
    if (arr.isEmpty()) {
        clearTabStripsForScreen(screenId);
        return;
    }
    // Emit-on-change: a relayout that resolved every window to the exact
    // rect already applied (focus move under Never-centering, redundant
    // scheduled retile) must not re-feed the compositor's apply path.
    if (anyRectMoved) {
        Q_EMIT windowsTiled(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    }

    // Tab-strip indicator model: one entry per VISIBLE tabbed column.
    // Change-gated: the payload is re-derived on every relayout, but an
    // unchanged strip set (the common focus-move / plain-retile case) must
    // not re-drive an overlay repaint — and an empty state is announced
    // exactly once (clearTabStripsForScreen latches on the tracked set).
    QJsonArray strips;
    for (const ResolvedColumn& column : resolved.columns) {
        if (!column.tabbed || !column.rect.intersects(params.workArea)) {
            continue;
        }
        QJsonObject strip;
        strip[QLatin1String("x")] = column.rect.x();
        strip[QLatin1String("y")] = column.rect.y();
        strip[QLatin1String("width")] = column.rect.width();
        QJsonArray tabs;
        // 0 is a safe seed, not a fallback: a resolved tabbed column always
        // carries exactly one non-hidden tile (relayout marks every tile
        // but the column's active one hidden), so the loop below always
        // overwrites it. An all-hidden column cannot be resolved — a
        // fully-minimized column is skipped before ResolvedColumns are
        // built.
        int activeIndex = 0;
        for (int i = 0; i < column.tiles.size(); ++i) {
            tabs.append(column.tiles.at(i).windowId);
            if (!column.tiles.at(i).hidden) {
                activeIndex = i;
            }
        }
        strip[QLatin1String("activeIndex")] = activeIndex;
        strip[QLatin1String("tabs")] = tabs;
        strips.append(strip);
    }
    if (!strips.isEmpty()) {
        const QString payload = QString::fromUtf8(QJsonDocument(strips).toJson(QJsonDocument::Compact));
        if (m_lastTabStripPayload.value(screenId) != payload) {
            m_lastTabStripPayload.insert(screenId, payload);
            m_screensWithTabStrips.insert(screenId);
            Q_EMIT tabStripsChanged(screenId, payload);
        }
    } else {
        clearTabStripsForScreen(screenId);
    }

    if (focusWindowAfter) {
        const QString active = state->strip().activeWindowId();
        if (!active.isEmpty()) {
            Q_EMIT activateWindowRequested(active);
        }
    }
}

} // namespace PhosphorScrollEngine
