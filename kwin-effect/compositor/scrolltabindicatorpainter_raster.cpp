// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrolltabindicatorpainter.h"

#include <QFontInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

// Layout + rasterisation of the scrolling strip's tab indicators.
//
// This is the visual spec for the pills. It was ported line by line from the
// daemon's QML rendering (ScrollTabStripContent.qml, deleted with the move to
// compositor-side drawing), so every choice below is the shipped look rather
// than a redesign: both styles fill the resolved rect exactly, the last tab
// absorbs the division remainder, interior joints square at zero gap, and
// the chip style carries a translucent backdrop with a hairline border. Also
// documented here are the raster-specific choices a QML reader never met:
// pen placement, premultiplied alpha and clipping.
namespace PlasmaZones {

namespace {

/// Qt Quick's Qt.alpha(): SETS the alpha, it does not scale the existing one.
QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

/// Alpha-weighted linear blend, @p t of the way from @p from to @p to: the
/// result is what compositing @p to over @p from at opacity @p t looks like.
/// Used only for the hover tint. A plain per-channel mix is wrong here
/// because the inactive chip colour is FULLY TRANSPARENT (#00000000): mixing
/// its black RGB into the highlight gave a tint that was one fifth of the
/// highlight's alpha AND one fifth of its colour, i.e. five times too dark
/// once QPainter premultiplied it. Weighting each operand's RGB by its own
/// alpha makes a transparent `from` contribute nothing but transparency.
QColor blend(const QColor& from, const QColor& to, qreal t)
{
    if (!from.isValid()) {
        return to;
    }
    if (!to.isValid()) {
        return from;
    }
    const qreal wa = from.alphaF() * (1.0 - t);
    const qreal wb = to.alphaF() * t;
    const qreal alpha = wa + wb;
    if (alpha <= 0.0) {
        return QColor(Qt::transparent);
    }
    QColor out;
    out.setRgbF((from.redF() * wa + to.redF() * wb) / alpha, (from.greenF() * wa + to.greenF() * wb) / alpha,
                (from.blueF() * wa + to.blueF() * wb) / alpha, alpha);
    return out;
}

/// Radius for a tab whose short extent is @p thickness, resolving the pill
/// sentinel (negative = fully rounded) and clamping so an oversized radius
/// cannot exceed a half extent and render as a lozenge with inverted corners.
qreal tabRadius(const ScrollTabIndicatorStyle& style, qreal thickness)
{
    const qreal half = thickness / 2.0;
    if (style.cornerRadius < 0) {
        return half;
    }
    return std::min(qreal(style.cornerRadius), half);
}

/// Colour for @p pill, resolving the same tiers the QML did: the window
/// rule's own colour, then the config/context colour, then the theme.
/// Urgency outranks the active highlight (a tab already in view needs no
/// highlight, whereas an urgent one the user cannot see is the whole point).
/// @p forBar picks the style-specific inactive fallback: a bar segment needs a
/// visible resting colour because the run IS the widget, while an inactive
/// chip is transparent and reads against the pill behind it.
///
/// The four THEME fallbacks (active = highlight, urgent = negativeText,
/// inactive bar = text at 0.35, inactive chip = transparent) are mirrored
/// by hand in src/shared/ZoneColorDefaults.qml, which the settings page's
/// swatch previews read. Change one, change both.
QColor resolveTabColor(const ScrollTabPill& pill, const ScrollTabIndicatorStyle& style, bool forBar, bool hovered)
{
    if (pill.urgent) {
        if (pill.urgentColor.isValid()) {
            return pill.urgentColor;
        }
        return style.urgentColor.isValid() ? style.urgentColor : style.themeNegativeText;
    }
    if (pill.active) {
        if (pill.activeColor.isValid()) {
            return pill.activeColor;
        }
        return style.activeColor.isValid() ? style.activeColor : style.themeHighlight;
    }
    QColor inactive;
    if (pill.inactiveColor.isValid()) {
        inactive = pill.inactiveColor;
    } else if (style.inactiveColor.isValid()) {
        inactive = style.inactiveColor;
    } else {
        inactive = forBar ? withAlpha(style.themeText, 0.35) : QColor(Qt::transparent);
    }
    if (!hovered) {
        return inactive;
    }
    // Hover is the ONE addition over the QML, which had no pointer feedback
    // because the layer-shell surface only carried click targets. Kept
    // deliberately subtle — a fifth of the way to the highlight — so a
    // hovered inactive tab reads as "reachable" without being mistaken for
    // the active one. Active and urgent tabs are left alone: they already
    // carry the strongest colours in the widget, and tinting them toward the
    // highlight would either do nothing or weaken the urgent signal.
    return blend(inactive, style.themeHighlight, 0.2);
}

/// Rounded-rect path with INDEPENDENT corner radii, which QPainter has no
/// primitive for (drawRoundedRect is uniform) and the segment bar needs: at
/// zero gap the run reads as one continuous bar, so only its two ends are
/// rounded and the interior joints stay square.
QPainterPath cornerPath(const QRectF& rect, qreal topLeft, qreal topRight, qreal bottomRight, qreal bottomLeft)
{
    const qreal limit = std::min(rect.width(), rect.height()) / 2.0;
    const qreal tl = std::clamp(topLeft, 0.0, limit);
    const qreal tr = std::clamp(topRight, 0.0, limit);
    const qreal br = std::clamp(bottomRight, 0.0, limit);
    const qreal bl = std::clamp(bottomLeft, 0.0, limit);

    QPainterPath path;
    path.moveTo(rect.left() + tl, rect.top());
    path.lineTo(rect.right() - tr, rect.top());
    if (tr > 0.0) {
        path.arcTo(QRectF(rect.right() - 2 * tr, rect.top(), 2 * tr, 2 * tr), 90.0, -90.0);
    }
    path.lineTo(rect.right(), rect.bottom() - br);
    if (br > 0.0) {
        path.arcTo(QRectF(rect.right() - 2 * br, rect.bottom() - 2 * br, 2 * br, 2 * br), 0.0, -90.0);
    }
    path.lineTo(rect.left() + bl, rect.bottom());
    if (bl > 0.0) {
        path.arcTo(QRectF(rect.left(), rect.bottom() - 2 * bl, 2 * bl, 2 * bl), 270.0, -90.0);
    }
    path.lineTo(rect.left(), rect.top() + tl);
    if (tl > 0.0) {
        path.arcTo(QRectF(rect.left(), rect.top(), 2 * tl, 2 * tl), 180.0, -90.0);
    }
    path.closeSubpath();
    return path;
}

/// Geometry shared by both styles, derived once per indicator.
struct IndicatorAxes
{
    bool vertical = false;
    bool leftEdge = false;
    int longExtent = 0;
    int shortExtent = 0;
    int tabCount = 0;
    /// Inter-tab gap, floored at 0 and then capped by axesOf() against the
    /// space the indicator has: a negative value can only arrive through a
    /// garbled per-screen override (the setting's own floor is 0), and below
    /// zero the segment offsets would walk backwards over each other, while a
    /// gap the run cannot afford would push the tail tabs outside the
    /// indicator to be clipped away. The cap can drive a nonzero setting to 0,
    /// which the segment bar then draws as one continuous run with square
    /// interior joints (the `axes.gaps == 0` test further down). That is the
    /// correct look for a bar with no room between its segments, not a
    /// regression.
    int gaps = 0;
};

IndicatorAxes axesOf(const ScrollTabIndicator& indicator, const ScrollTabIndicatorStyle& style)
{
    IndicatorAxes axes;
    axes.vertical = indicator.position == 0 || indicator.position == 1;
    axes.leftEdge = indicator.position == 0;
    axes.longExtent = axes.vertical ? indicator.rect.height() : indicator.rect.width();
    axes.shortExtent = axes.vertical ? indicator.rect.width() : indicator.rect.height();
    axes.tabCount = int(indicator.tabs.size());
    axes.gaps = std::max(0, style.gapsBetweenTabs);
    // Then capped against the space there actually is. Every per-tab share
    // floors at 1px, but tabOffset() still accumulates the FULL gap per tab,
    // so a gap the run cannot afford walks the tail off the end of the
    // indicator — where the clip discards it and layoutPills intersects it
    // away, leaving those tabs undrawn AND unclickable. The cap leaves room
    // for one pixel per tab plus the gaps between them. It is applied here, in
    // the one place both styles and the hit test read their axes from, so draw
    // rects and hit rects can never disagree about the gap.
    if (axes.tabCount > 1) {
        axes.gaps = std::min(axes.gaps, std::max(0, (axes.longExtent - axes.tabCount) / (axes.tabCount - 1)));
    }
    return axes;
}

/// Chip-style geometry (the pill's insets and each chip's share of the long
/// axis). Mirrors the pill's chipInset / chipThickness / chipLongBudget /
/// chipTrailingBudget properties.
struct ChipMetrics
{
    int inset = 1;
    int thickness = 1;
    int longBudget = 1;
    int trailingBudget = 1;
};

ChipMetrics chipMetricsOf(const IndicatorAxes& axes, const ScrollTabIndicatorStyle& style)
{
    ChipMetrics metrics;
    metrics.inset = std::max(1, int(std::lround(style.smallSpacing / 2.0)));
    metrics.thickness = std::max(1, axes.shortExtent - metrics.inset * 2);
    const int neighbours = std::max(0, axes.tabCount - 1);
    const int inner = axes.longExtent - metrics.inset * 2;
    // Floored at 1, NOT at some legible minimum: a floor big enough to keep a
    // title readable would let the run overflow the pill once there were
    // enough tabs, and an overflowing run is clipped — those tabs would be
    // undrawn AND unclickable, which is worse than thin. While the indicator
    // has at least one pixel per tab, axesOf()'s gap cap keeps the run inside
    // the pill; with fewer pixels than tabs the 1px floor cannot help and the
    // tail is clipped regardless. The chip arm budgets against `inner`, so
    // even inside the cap the last chip's edge can run up to one inset (1-2px
    // at the default spacing) past the pill and lose that edge to the clip.
    // One clipped edge, never a lost tab.
    metrics.longBudget = std::max(1, (inner - axes.gaps * neighbours) / std::max(1, axes.tabCount));
    // The last chip absorbs the division remainder so the run ends flush with
    // the pill's inner edge instead of leaving up to tabCount-1 px that
    // belongs to no tab.
    metrics.trailingBudget = std::max(1, inner - (metrics.longBudget + axes.gaps) * neighbours);
    return metrics;
}

/// Vertical breathing room, in logical px, left between the chip's two long
/// edges and the label's line box (one px each side). Small on purpose: the
/// chip is already inset from the pill, so this only has to stop the
/// ascender and descender from touching the chip's rounded ends.
constexpr qreal kLabelChipMargin = 2.0;

/// The chip label font and its metrics, fitted to one chip thickness.
struct FittedLabel
{
    int thickness = 0;
    QFont font;
    QFontMetricsF metrics;
};

/// @p base re-sized so its rendered line height fits inside a chip of
/// @p thickness logical px, measured on @p device (the same paint device the
/// text is drawn on, so the fit, the elide width and the rendered glyphs all
/// resolve against one font engine).
///
/// The estimate-then-verify shape is deliberate. A fixed pixelSize-to-height
/// ratio is NOT safe across families: height() is ascent + descent + leading,
/// and how much of the em box each takes varies by typeface (and by hinting
/// at small sizes), so a ratio tuned on one font overflows on another. So the
/// ratio is MEASURED from this very font at its own size, used to land the
/// first guess, and then the guess is checked by measurement and walked down
/// until it really fits. The walk terminates: every step decrements, and the
/// floor is 1px. A font that cannot honour pixelSize at all (a bitmap face)
/// walks to that floor and is then clipped by the indicator clip rect, which
/// is the same outcome the un-fitted code had.
///
/// The fit is a property of the FONT, not of the STRING. A title Qt renders
/// through its fallback chain — a script this family does not cover — can
/// report a taller line box than the fitted face does, and the excess is
/// clipped by the indicator clip rect. Keying the memo on the title as well
/// would cost a fit per tab instead of one per indicator, so what the fitted
/// size promises is that THIS font fits, not that every title will.
FittedLabel fitLabelFont(const QFont& base, int thickness, const QPaintDevice* device)
{
    const qreal budget = std::max(1.0, qreal(thickness) - kLabelChipMargin);
    QFont probe = base;
    // Measure the ratio in PIXEL terms: the base font is usually point-sized
    // (it comes from the system font database), and a point size cannot be
    // scaled against a pixel budget without knowing the device's DPI.
    const int probeSize = std::max(1, QFontInfo(base).pixelSize());
    probe.setPixelSize(probeSize);
    const qreal probeHeight = QFontMetricsF(probe, device).height();

    int pixelSize = probeSize;
    if (probeHeight > 0.0) {
        pixelSize = int(std::floor(probeSize * budget / probeHeight));
    }
    pixelSize = std::max(1, pixelSize);

    QFont font = base;
    font.setPixelSize(pixelSize);
    QFontMetricsF metrics(font, device);
    while (pixelSize > 1 && metrics.height() > budget) {
        --pixelSize;
        font.setPixelSize(pixelSize);
        metrics = QFontMetricsF(font, device);
    }
    return {thickness, font, metrics};
}

/// Offset along the long axis of tab @p index, in indicator-local px.
int tabOffset(int index, int perTab, int gaps)
{
    return index * (perTab + gaps);
}

/// Turn an (offset, extent) pair along the indicator's long axis into a rect
/// in absolute logical coordinates. @p acrossOffset / @p acrossExtent are the
/// short-axis pair.
QRect axisRect(const ScrollTabIndicator& indicator, const IndicatorAxes& axes, int alongOffset, int alongExtent,
               int acrossOffset, int acrossExtent)
{
    if (axes.vertical) {
        return QRect(indicator.rect.x() + acrossOffset, indicator.rect.y() + alongOffset, acrossExtent, alongExtent);
    }
    return QRect(indicator.rect.x() + alongOffset, indicator.rect.y() + acrossOffset, alongExtent, acrossExtent);
}

/// The UNCLIPPED tab rects, in draw order. layoutPills clips these to the
/// indicator for the hit rects; rasterise draws them under a clip to the
/// same rect, so both derive from the one list.
QVector<QRect> tabRects(const ScrollTabIndicator& indicator, const IndicatorAxes& axes,
                        const ScrollTabIndicatorStyle& style)
{
    QVector<QRect> out;
    out.reserve(axes.tabCount);
    if (style.style == 1) {
        // Segment bar: each segment takes an equal share of the long axis
        // after the inter-tab gaps are removed, floored at 1 so a bar too
        // short for its tab count still carries every segment rather than
        // silently dropping the tail to zero. While the bar has at least one
        // pixel per tab, axesOf()'s gap cap is what keeps the run inside it —
        // the floor alone does not, because the offsets still accumulate the
        // gap. Below one pixel per tab the tail segments are clipped and
        // nothing here can prevent it.
        const int baseLength = std::max(1, (axes.longExtent - axes.gaps * (axes.tabCount - 1)) / axes.tabCount);
        for (int i = 0; i < axes.tabCount; ++i) {
            const int offset = tabOffset(i, baseLength, axes.gaps);
            const int length = i == axes.tabCount - 1 ? std::max(1, axes.longExtent - offset) : baseLength;
            out.append(axisRect(indicator, axes, offset, length, 0, axes.shortExtent));
        }
        return out;
    }
    const ChipMetrics metrics = chipMetricsOf(axes, style);
    for (int i = 0; i < axes.tabCount; ++i) {
        const int along = i == axes.tabCount - 1 ? metrics.trailingBudget : metrics.longBudget;
        const int offset = metrics.inset + tabOffset(i, metrics.longBudget, axes.gaps);
        out.append(axisRect(indicator, axes, offset, along, metrics.inset, metrics.thickness));
    }
    return out;
}

} // namespace

namespace ScrollTabRaster {

QVector<ScrollTabHitRect> layoutPills(const ScrollTabIndicator& indicator, const ScrollTabIndicatorStyle& style)
{
    QVector<ScrollTabHitRect> out;
    const IndicatorAxes axes = axesOf(indicator, style);
    if (axes.tabCount <= 0 || indicator.rect.isEmpty()) {
        return out;
    }
    const QVector<QRect> rects = tabRects(indicator, axes, style);
    out.reserve(rects.size());
    for (int i = 0; i < rects.size(); ++i) {
        // The QML indicator clips, and the daemon's input region was exactly
        // the indicator rect, so a run that overflowed was neither drawn nor
        // clickable past the edge. Intersecting keeps the hit rects honest
        // about that.
        out.append({rects.at(i).intersected(indicator.rect), indicator.tabs.at(i).windowId});
    }
    return out;
}

QImage rasterise(const QVector<ScrollTabIndicator>& indicators, const ScrollTabIndicatorStyle& style,
                 const QRect& bounds, qreal devicePixelRatio, const QString& hoveredWindowId)
{
    if (bounds.isEmpty() || indicators.isEmpty()) {
        return {};
    }
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const QSize deviceSize(int(std::ceil(bounds.width() * dpr)), int(std::ceil(bounds.height() * dpr)));
    if (deviceSize.isEmpty()) {
        return {};
    }
    // ARGB32_Premultiplied is not a preference: QPainter's source-over
    // compositing is DEFINED for premultiplied targets, and every colour here
    // is potentially translucent (the pill background at 0.85, the hairline
    // border at 0.2, an inactive bar segment at 0.35, a fully transparent
    // inactive chip). Rasterising straight-alpha would produce wrong colours
    // at every partially-covered antialiased edge. The blit then uses the
    // matching premultiplied blend func, so the format is load-bearing at
    // both ends.
    QImage image(deviceSize, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return {};
    }
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    // Draw in ABSOLUTE logical coordinates throughout; the image simply is
    // the window onto them at `bounds`.
    painter.translate(-bounds.topLeft());
    // The label font is NOT hoisted, because it is not one font: the chip
    // style sizes it to the chip's thickness, which is per indicator. Fitted
    // fonts are memoised by thickness for the length of this call — the
    // indicators of one output nearly always share a thickness (it comes from
    // the one Width setting), so in practice this is one fit per raster, and
    // a linear scan over a handful of entries is cheaper than any keyed
    // container. The cache is per-call by design: the style's font can change
    // between rasters and a longer-lived cache would need its own
    // invalidation. The segment-bar style returns before any of this and pays
    // nothing.
    std::vector<FittedLabel> fitted;

    for (const ScrollTabIndicator& indicator : indicators) {
        const IndicatorAxes axes = axesOf(indicator, style);
        if (axes.tabCount <= 0 || indicator.rect.isEmpty()) {
            continue;
        }
        // Sub-rect rasters (the hover patch) hand in a `bounds` that covers
        // one indicator; anything outside it is wasted QPainter work.
        if (!indicator.rect.intersects(bounds)) {
            continue;
        }
        const QVector<QRect> rects = tabRects(indicator, axes, style);
        painter.save();
        // Both styles fill the resolved rect exactly, and a rect too short
        // for its tab count produces a run LONGER than the rect (every share
        // floors at 1px). Clipping is what keeps that overflow off the
        // neighbouring window, exactly as the QML delegate's clip did.
        painter.setClipRect(indicator.rect);
        painter.setPen(Qt::NoPen);

        if (style.style == 1) {
            const qreal radius = tabRadius(style, axes.shortExtent);
            for (int i = 0; i < axes.tabCount; ++i) {
                const ScrollTabPill& pill = indicator.tabs.at(i);
                const QRectF rect = rects.at(i);
                // With no gap the run reads as one continuous bar, so only
                // its two ends are rounded. Top-left is the leading corner
                // and bottom-right the trailing one in BOTH orientations, so
                // those two need no branch; only the other diagonal swaps.
                const bool squareLeading = axes.gaps == 0 && i > 0;
                const bool squareTrailing = axes.gaps == 0 && i < axes.tabCount - 1;
                const qreal tl = squareLeading ? 0.0 : radius;
                const qreal tr = (axes.vertical ? squareLeading : squareTrailing) ? 0.0 : radius;
                const qreal bl = (axes.vertical ? squareTrailing : squareLeading) ? 0.0 : radius;
                const qreal br = squareTrailing ? 0.0 : radius;
                const bool hovered = !hoveredWindowId.isEmpty() && pill.windowId == hoveredWindowId;
                painter.fillPath(cornerPath(rect, tl, tr, br, bl), resolveTabColor(pill, style, true, hovered));
            }
            painter.restore();
            continue;
        }

        // ── Title-chip pill ──────────────────────────────────────────────
        const ChipMetrics chip = chipMetricsOf(axes, style);
        const qreal pillRadius = tabRadius(style, axes.shortExtent);
        // The 1px border is a deliberate hairline. Qt Quick draws a
        // Rectangle's border fully INSIDE its bounds, while QPainter centres
        // a pen on the path, so the path is inset by half the pen width to
        // land in the same pixels.
        const QRectF pillRect = QRectF(indicator.rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const QPainterPath pillPath =
            cornerPath(pillRect, pillRadius - 0.5, pillRadius - 0.5, pillRadius - 0.5, pillRadius - 0.5);
        painter.fillPath(pillPath, withAlpha(style.themeBackground, 0.85));
        painter.setPen(QPen(withAlpha(style.themeText, 0.2), 1.0));
        painter.drawPath(pillPath);
        painter.setPen(Qt::NoPen);

        const qreal chipRadius = tabRadius(style, chip.thickness);
        // Fit the label to THIS indicator's chip thickness (memoised above).
        // Done once per indicator rather than per tab: every chip of one
        // indicator shares the thickness, which is the indicator's short
        // extent minus the inset.
        auto fittedIt = std::find_if(fitted.begin(), fitted.end(), [&](const FittedLabel& f) {
            return f.thickness == chip.thickness;
        });
        if (fittedIt == fitted.end()) {
            fitted.push_back(fitLabelFont(style.font, chip.thickness, &image));
            // Re-derived AFTER the push: a growing vector reallocates, which
            // would leave the iterator above dangling.
            fittedIt = std::prev(fitted.end());
        }
        const FittedLabel& label = *fittedIt;
        painter.setFont(label.font);
        const QFontMetricsF& metrics = label.metrics;

        for (int i = 0; i < axes.tabCount; ++i) {
            const ScrollTabPill& pill = indicator.tabs.at(i);
            const QRectF rect = rects.at(i);
            const int along = axes.vertical ? rects.at(i).height() : rects.at(i).width();
            const bool hovered = !hoveredWindowId.isEmpty() && pill.windowId == hoveredWindowId;
            painter.fillPath(cornerPath(rect, chipRadius, chipRadius, chipRadius, chipRadius),
                             resolveTabColor(pill, style, false, hovered));

            if (pill.title.isEmpty()) {
                continue;
            }
            // The label elides against the chip's SHARE of the indicator, not
            // a fixed cap, so lengthening the indicator shows more of the
            // titles instead of just adding empty space.
            const qreal cap = std::max(1.0, qreal(along - style.largeSpacing));
            const qreal natural = metrics.horizontalAdvance(pill.title);
            const qreal width = std::min(natural, cap);
            const QString text = metrics.elidedText(pill.title, Qt::ElideRight, width);
            const qreal height = metrics.height();
            // Rotated on a side edge so the title reads down the column
            // instead of being clipped to the indicator's thickness. The
            // direction follows Qt's own vertical-tab convention:
            // bottom-to-top on a left edge, top-to-bottom on a right one, so
            // the text always leans away from the window it labels. Rotation
            // is about the label's centre, which is the chip's centre.
            const qreal angle = !axes.vertical ? 0.0 : (axes.leftEdge ? -90.0 : 90.0);
            painter.save();
            painter.translate(rect.center());
            if (angle != 0.0) {
                painter.rotate(angle);
            }
            painter.setPen(QPen(pill.active || pill.urgent ? style.themeHighlightedText : style.themeText));
            // Widened symmetrically for the CLIP only. drawText clips to this
            // rect, and a slanted or swashed final glyph can carry ink past
            // its own advance, which `width` is measured in — newly visible
            // now that italic is a user setting. AlignCenter means the text
            // still draws in exactly the same place, in both orientations;
            // only the boundary moves, and only bearing ink can appear in the
            // margin. Qt::TextDontClip is deliberately NOT used: that would
            // also let a mis-measured elide spill across the neighbouring
            // chip. Widened on the text axis only — the vertical extent is the
            // fitted line box, and growing it would defeat the fit.
            const qreal clipSlack = height;
            painter.drawText(QRectF(-width / 2.0 - clipSlack, -height / 2.0, width + clipSlack * 2.0, height),
                             Qt::AlignCenter, text);
            painter.restore();
        }
        painter.restore();
    }
    painter.end();
    return image;
}

} // namespace ScrollTabRaster

} // namespace PlasmaZones
