// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

#include <memory>
#include <unordered_map>

namespace KWin {
class GLTexture;
class LogicalOutput;
class Region;
class RenderTarget;
class RenderViewport;
}

namespace PlasmaZones {

/// Paint-time description of ONE tab of one indicator.
///
/// `title` is drawn VERBATIM. The empty-title fallback ("Untitled window")
/// is the caller's to resolve, because it is a translated string and this
/// component deliberately carries no i18n dependency — it is the drawing
/// half of the tab indicators and nothing else.
struct ScrollTabPill
{
    /// Canonical window id, opaque here. Hit-test answers return it.
    QString windowId;
    /// Already-resolved label (caption, or the app-id/untitled fallback).
    QString title;
    bool active = false;
    bool urgent = false;
    /// Pointer hover, as pushed by the caller. The painter ALSO tracks its
    /// own hover through setHover(); a pill is drawn hovered if either says
    /// so, so a caller that never calls setHover and a caller that only
    /// calls setHover both work.
    bool hovered = false;
    // Per-window rule colours. An invalid QColor means "unset" and falls
    // through to the strip/config colour, then to the theme.
    QColor activeColor;
    QColor inactiveColor;
    QColor urgentColor;

    bool operator==(const ScrollTabPill& other) const
    {
        return windowId == other.windowId && title == other.title && active == other.active && urgent == other.urgent
            && hovered == other.hovered && activeColor == other.activeColor && inactiveColor == other.inactiveColor
            && urgentColor == other.urgentColor;
    }
    bool operator!=(const ScrollTabPill& other) const
    {
        return !(*this == other);
    }
};

/// One indicator: the bar or chip run belonging to one tabbed column.
struct ScrollTabIndicator
{
    /// The rect the scrolling engine resolved for this indicator, in
    /// ABSOLUTE LOGICAL screen coordinates. Both styles fill it exactly on
    /// both axes — the engine reserves precisely these pixels out of the
    /// column when "place within column" is set, so anything drawn outside
    /// would overlap the window. Content that does not fit is CLIPPED to it.
    QRect rect;
    /// 0 Left, 1 Right, 2 Top, 3 Bottom. Left/Right run the tabs DOWN the
    /// column, so the bar is vertical and chip titles rotate.
    int position = 0;
    QVector<ScrollTabPill> tabs;

    bool operator==(const ScrollTabIndicator& other) const
    {
        return rect == other.rect && position == other.position && tabs == other.tabs;
    }
    bool operator!=(const ScrollTabIndicator& other) const
    {
        return !(*this == other);
    }
};

/// Paint settings, resolved by the caller from the daemon's
/// Scrolling.TabIndicator settings layered with any per-screen context
/// override. None of these can change a resolved rect, which is why they
/// travel straight from config to the renderer.
struct ScrollTabIndicatorStyle
{
    /// 0 = title chips, 1 = segment bar.
    int style = 1;
    /// Gap between individual tabs, in logical pixels.
    int gapsBetweenTabs = 0;
    /// Per-tab corner radius. NEGATIVE means fully rounded (half the tab's
    /// short extent), which is how the chips pill has always looked.
    int cornerRadius = 0;
    // Config-tier colours. An invalid QColor means "follow the theme".
    QColor activeColor;
    QColor inactiveColor;
    QColor urgentColor;
    // Theme palette, injected rather than read from a KColorScheme here so
    // the component stays a pure function of its inputs (and so the
    // settings page's swatch previews and this renderer cannot drift).
    QColor themeHighlight;
    QColor themeHighlightedText;
    QColor themeText;
    QColor themeBackground;
    QColor themeNegativeText;
    /// Already scaled by the caller from family / size scale / weight /
    /// italic / underline / strikeout.
    QFont font;
    /// Kirigami.Units.smallSpacing equivalent (Kirigami is not available in
    /// the compositor, so the caller supplies the number).
    int smallSpacing = 4;
    /// Kirigami.Units.largeSpacing equivalent.
    int largeSpacing = 12;

    bool operator==(const ScrollTabIndicatorStyle& other) const
    {
        return style == other.style && gapsBetweenTabs == other.gapsBetweenTabs && cornerRadius == other.cornerRadius
            && activeColor == other.activeColor && inactiveColor == other.inactiveColor
            && urgentColor == other.urgentColor && themeHighlight == other.themeHighlight
            && themeHighlightedText == other.themeHighlightedText && themeText == other.themeText
            && themeBackground == other.themeBackground && themeNegativeText == other.themeNegativeText
            && font == other.font && smallSpacing == other.smallSpacing && largeSpacing == other.largeSpacing;
    }
    bool operator!=(const ScrollTabIndicatorStyle& other) const
    {
        return !(*this == other);
    }
};

/// One tab's hit rect, in ABSOLUTE LOGICAL coordinates and already clipped
/// to its indicator's rect (the QML the raster mirrors clips its indicators,
/// so a tab run that overflows a too-short rect is neither drawn nor
/// clickable past the edge).
struct ScrollTabHitRect
{
    QRect rect;
    QString windowId;
};

/// Rasterisation and layout, split out of the class so the GL half stays
/// readable. Both entry points are pure functions of their inputs.
namespace ScrollTabRaster {

/// Hit rects for every tab of @p indicator, in absolute logical coordinates,
/// in the same order the tabs are drawn.
QVector<ScrollTabHitRect> layoutPills(const ScrollTabIndicator& indicator, const ScrollTabIndicatorStyle& style);

/// Rasterise @p indicators into an ARGB32-premultiplied image covering
/// @p bounds at @p devicePixelRatio. Drawing happens in image-local
/// coordinates (absolute minus bounds.topLeft()).
///
/// @p hoveredWindowId additionally marks that window's pill hovered, on top
/// of any pill whose own `hovered` flag is set.
QImage rasterise(const QVector<ScrollTabIndicator>& indicators, const ScrollTabIndicatorStyle& style,
                 const QRect& bounds, qreal devicePixelRatio, const QString& hoveredWindowId);

} // namespace ScrollTabRaster

/**
 * @brief Draws the scrolling strip's tab indicators inside the compositor.
 *
 * The daemon used to draw these in QML into a layer-shell surface that the
 * compositor then slid, which always lagged window motion by a frame. This
 * class rasterises the same visual into one texture per output and blits it
 * in the effect's own paint pass, so the pills move on exactly the frame the
 * windows do.
 *
 * @par Ownership and threading
 * Pure component: it knows nothing about PlasmaZonesEffect. Everything
 * happens on the compositor thread; the only GL calls live in paint() and
 * releaseGl(), both of which require a current context.
 *
 * @par Coordinate spaces
 * The model, the bounds and the hit rects are all in ABSOLUTE LOGICAL screen
 * coordinates, exactly as the scrolling engine resolved them. The view
 * offset (the strip view spring's offsetFor()) is applied at BLIT time and
 * at hit-test time, never baked into the model — the model only changes when
 * the strip relayouts, whereas the offset changes every frame of a scroll.
 */
class ScrollTabIndicatorPainter
{
public:
    ScrollTabIndicatorPainter();
    ~ScrollTabIndicatorPainter();

    ScrollTabIndicatorPainter(const ScrollTabIndicatorPainter&) = delete;
    ScrollTabIndicatorPainter& operator=(const ScrollTabIndicatorPainter&) = delete;

    /// Replace the model for @p output. The texture is marked dirty only
    /// when the model or the style actually differs from what was last
    /// rasterised, so this is cheap to call every frame with unchanged data.
    void setIndicators(KWin::LogicalOutput* output, const QVector<ScrollTabIndicator>& indicators,
                       const ScrollTabIndicatorStyle& style);

    /// Set (or clear) hover on the pill containing @p pos. Returns true when
    /// the hover changed and the caller should repaint.
    ///
    /// @p pos is absolute logical, in the same space the pointer reports.
    /// @p viewOffset must be the SAME offset paint() is given for this
    /// output, because the pills are on screen shifted by it. The default is
    /// the at-rest case (no scroll in flight); pass the live offset whenever
    /// one exists or hover will latch onto the wrong pill mid-scroll.
    bool setHover(KWin::LogicalOutput* output, const QPointF& pos, const QPointF& viewOffset = QPointF());

    /// windowId of the pill under @p pos (absolute logical), empty if none.
    /// @p viewOffset is the same offset the blit uses.
    QString pillAt(KWin::LogicalOutput* output, const QPointF& pos, const QPointF& viewOffset) const;

    /// Union of @p output's indicator rects, absolute logical and WITHOUT
    /// the view offset — the caller adds the offset when turning this into a
    /// damage region, exactly as paint() does when placing the quad.
    QRect boundsFor(KWin::LogicalOutput* output) const;

    bool hasIndicators(KWin::LogicalOutput* output) const;

    /// Drop @p output's model and texture (output removed, or its strip is
    /// gone). Deletes GL resources, so the context must be current.
    void clearOutput(KWin::LogicalOutput* output);

    /// Drop every output's model. Textures are released too, so this also
    /// needs a current context.
    void clearAll();

    /// Release every GL resource while keeping the models. Must be called
    /// with the GL context current, on teardown, before the context dies.
    void releaseGl();

    /// Draw @p output's indicators translated by @p viewOffset (logical px).
    /// Called from the effect's paint pass with @p renderTarget /
    /// @p viewport current. Re-rasterises and re-uploads lazily when dirty.
    /// @p deviceRegion is the triggering paintWindow call's device-space
    /// damage region; the blit is hardware-clipped to it, never painted
    /// unclipped. Paint order only yields stacking order when every window
    /// above repaints the same pixels afterwards, and KWin hands each of them
    /// only the damage region — pill pixels outside it would surface over
    /// whatever is stacked above the strip until the next full-damage frame.
    void paint(KWin::LogicalOutput* output, const KWin::RenderTarget& renderTarget,
               const KWin::RenderViewport& viewport, const KWin::Region& deviceRegion, const QPointF& viewOffset);

private:
    /// Everything the painter keeps for one output. The model half is plain
    /// data; the texture half is GL-owned and only ever touched from paint()
    /// / releaseGl() / clearOutput().
    struct PerOutput
    {
        QVector<ScrollTabIndicator> indicators;
        ScrollTabIndicatorStyle style;
        /// Flattened hit rects for every tab of every indicator, absolute
        /// logical, recomputed whenever the model or style changes.
        QVector<ScrollTabHitRect> hits;
        /// Union of the indicator rects, absolute logical.
        QRect bounds;
        /// Hover is owned HERE rather than in the pushed model: the caller
        /// pushes a fresh model on every relayout and would wipe it.
        QString hoveredWindowId;
        /// Set when the rasterised image no longer matches the model, the
        /// style, the hover or the scale.
        bool dirty = true;
        std::unique_ptr<KWin::GLTexture> texture;
        /// Bounds and device-pixel ratio the live texture was rasterised
        /// for. A change in either is a re-rasterise, not just a re-upload.
        QRect textureBounds;
        qreal textureScale = 0.0;
    };

    PerOutput* find(KWin::LogicalOutput* output);
    const PerOutput* find(KWin::LogicalOutput* output) const;

    /// Recompute `hits` and `bounds` from the model, and drop a hover whose
    /// window is no longer on the strip.
    static void rebuildLayout(PerOutput& entry);

    std::unordered_map<KWin::LogicalOutput*, PerOutput> m_outputs;
};

} // namespace PlasmaZones
