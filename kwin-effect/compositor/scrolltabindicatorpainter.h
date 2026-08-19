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
#include <vector>

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
///
/// Hover is NOT part of the pushed model: the painter owns it (setHover),
/// because the caller pushes a fresh model on every relayout and would wipe
/// it. A pill is drawn hovered when the painter's own hover names it.
struct ScrollTabPill
{
    /// Canonical window id, opaque here. Hit-test answers return it.
    QString windowId;
    /// Already-resolved label (caption, or the untitled fallback).
    QString title;
    bool active = false;
    bool urgent = false;
    // Per-window rule colours. An invalid QColor means "unset" and falls
    // through to the strip/config colour, then to the theme.
    QColor activeColor;
    QColor inactiveColor;
    QColor urgentColor;

    bool operator==(const ScrollTabPill& other) const
    {
        return windowId == other.windowId && title == other.title && active == other.active && urgent == other.urgent
            && activeColor == other.activeColor && inactiveColor == other.inactiveColor
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
    /// Gap between individual tabs, in logical pixels. Negative values are
    /// treated as 0 by the layout (the setting's floor is 0; a negative one
    /// can only arrive through a garbled override).
    int gapsBetweenTabs = 0;
    /// Per-tab corner radius. NEGATIVE means fully rounded (half the tab's
    /// short extent), which is how the chips pill has always looked.
    int cornerRadius = 0;
    // Config-tier colours. An invalid QColor means "follow the theme".
    QColor activeColor;
    QColor inactiveColor;
    QColor urgentColor;
    // Theme palette, injected rather than read from a KColorScheme here so
    // the component stays a pure function of its inputs. The theme FALLBACK
    // rules (active = highlight, urgent = negativeText, inactive bar =
    // text at 0.35, inactive chip = transparent) live in resolveTabColor()
    // in scrolltabindicatorpainter_raster.cpp and are MIRRORED by hand in
    // src/shared/ZoneColorDefaults.qml for the settings page's swatches:
    // change one, change both.
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
    /// Kirigami.Units.largeSpacing equivalent: Kirigami derives it as
    /// smallSpacing * 2 (8 at the default font), and so does the caller.
    int largeSpacing = 8;

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
/// in the same order the tabs are drawn. Both this and rasterise() derive
/// their rects from the same internal tab-rect list (these are that list
/// clipped to the indicator; the raster draws the unclipped list under a
/// clip to the same rect), so hit-testing and drawing cannot drift apart.
QVector<ScrollTabHitRect> layoutPills(const ScrollTabIndicator& indicator, const ScrollTabIndicatorStyle& style);

/// Rasterise @p indicators into an ARGB32-premultiplied image covering
/// @p bounds at @p devicePixelRatio. Drawing happens in image-local
/// coordinates (absolute minus bounds.topLeft()).
///
/// @p hoveredWindowId marks that window's pill hovered.
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
 * @par What the move gave up
 * The QML pills were real Qt Quick items with an accessible role and name
 * per tab; a rasterised blit has no accessibility surface at all. The pills
 * stay interactive (hover, click, tap) through the effect's input hooks, but
 * a screen reader cannot reach them. This is a known trade of the compositor
 * move, recorded here so it is not mistaken for an omission.
 *
 * @par Ownership and threading
 * Pure component: it knows nothing about PlasmaZonesEffect. Everything
 * happens on the compositor thread. The ONLY GL calls live in paint() and
 * releaseGl(), both of which require a current context. clearOutput() and
 * clearAll() are GL-free: a dropped output's texture is RETIRED to a
 * graveyard and deleted by the next paint() or by releaseGl(), so the clear
 * paths may run from any D-Bus slot or Qt signal without a context.
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
    /// Requires releaseGl() to have run under a current context first (the
    /// effect's teardown does), so no GL name is deleted off-context here.
    ~ScrollTabIndicatorPainter();

    ScrollTabIndicatorPainter(const ScrollTabIndicatorPainter&) = delete;
    ScrollTabIndicatorPainter& operator=(const ScrollTabIndicatorPainter&) = delete;

    /// Replace the model for @p output. Returns true when the model or the
    /// style actually differed from what was last pushed (the caller damages
    /// on that edge only); false is a verified no-op, so this is cheap to
    /// call every frame with unchanged data.
    bool setIndicators(KWin::LogicalOutput* output, const QVector<ScrollTabIndicator>& indicators,
                       const ScrollTabIndicatorStyle& style);

    /// Set (or clear) hover on the pill containing @p pos. Returns true when
    /// the hover changed and the caller should repaint. Only the indicator
    /// rects that lost and gained the hover are re-rasterised (texture
    /// sub-update), never the whole union.
    ///
    /// @p pos is absolute logical, in the same space the pointer reports.
    /// @p viewOffset must be the SAME offset paint() is given for this
    /// output, because the pills are on screen shifted by it. The default is
    /// the at-rest case (no scroll in flight); pass the live offset whenever
    /// one exists or hover will latch onto the wrong pill mid-scroll.
    /// @p hitWindowId, when given, receives the pill under @p pos (the same
    /// answer pillAt() would give) so a caller that needs both does one scan.
    bool setHover(KWin::LogicalOutput* output, const QPointF& pos, const QPointF& viewOffset = QPointF(),
                  QString* hitWindowId = nullptr);

    /// windowId of the pill under @p pos (absolute logical), empty if none.
    /// @p viewOffset is the same offset the blit uses. When two indicator
    /// rects overlap, the one drawn LAST (topmost) wins, matching the raster.
    QString pillAt(KWin::LogicalOutput* output, const QPointF& pos, const QPointF& viewOffset) const;

    /// Union of @p output's indicator rects, absolute logical and WITHOUT
    /// the view offset. At rest the offset is zero, so this is exactly the
    /// on-screen rect and the right damage region; while a view leg is in
    /// flight the strip view spring's own repaint pump damages the whole
    /// output every frame (pack or no pack), so the shifted position needs
    /// no separate damage from the caller.
    QRect boundsFor(KWin::LogicalOutput* output) const;

    bool hasIndicators(KWin::LogicalOutput* output) const;
    /// True when ANY output currently has indicators. Used by the effect's
    /// isActive() predicate: the pills only exist while the effect is in the
    /// paint chain.
    bool hasAnyIndicators() const;

    /// Whether the last completed paint pass for @p output ISSUED the pill
    /// blit (set by notePassOutcome). "Issued", not "visible": the blit may
    /// have been overdrawn by a window stacked above the strip, which the
    /// input side checks separately against the stacking order. A model can
    /// exist with no anchor to blit at (every column parked, mode teardown
    /// race), and a pointer over a pill nothing blitted must neither show
    /// the hand nor eat a click.
    bool paintedLastPass(KWin::LogicalOutput* output) const;
    /// Record whether the pass that just finished on @p output blitted.
    void notePassOutcome(KWin::LogicalOutput* output, bool painted);
    /// Delete the textures retired by clearOutput()/clearAll() since the last
    /// GL-current point. paint() does this on its own; the effect also calls
    /// this right after each clear (under a made-current context) and at the
    /// top of paintScreen, because once the last indicator is gone the
    /// effect can drop out of the paint chain and paint() is never reached
    /// again. Requires a current context.
    void drainRetiredTextures();

    /// Drop @p output's model (output removed, or its strip is gone). Its
    /// texture is retired, not deleted: GL-free, safe off-context.
    void clearOutput(KWin::LogicalOutput* output);

    /// Drop every output's model. Textures are retired, not deleted:
    /// GL-free, safe off-context.
    void clearAll();

    /// Release every GL resource (live textures and the retired ones) while
    /// keeping the models, and forget every per-context fact (the failure
    /// latches, the texture size limit). Must be called with the GL context
    /// current, on teardown, before the context dies. Surviving models
    /// re-rasterise on their next paint.
    void releaseGl();

    /// Draw @p output's indicators translated by @p viewOffset (logical px).
    /// Called from the effect's paint pass with @p renderTarget /
    /// @p viewport current; the target's colour description drives the
    /// sRGB → output conversion so the pills match the windows on HDR and
    /// wide-gamut outputs. Re-rasterises and re-uploads lazily when dirty,
    /// and first deletes any retired textures (this is the GL-current point).
    /// @p clipRegion is the DEVICE-space region of the scene walk in
    /// progress (the pass's damage region); the blit is hardware-clipped to
    /// it, never painted unclipped. Paint order only yields stacking order
    /// when every window above repaints the same pixels afterwards, and KWin
    /// hands each of them only the damage region — pill pixels outside it
    /// would surface over whatever is stacked above the strip until the next
    /// full-damage frame.
    /// Returns true only when the blit was actually issued: a latched
    /// raster/upload failure, an over-limit bounds, or a missing texture
    /// return false, and the caller's pass outcome (what gates pill input)
    /// must record that nothing is on screen.
    bool paint(KWin::LogicalOutput* output, const KWin::RenderTarget& renderTarget,
               const KWin::RenderViewport& viewport, const KWin::Region& clipRegion, const QPointF& viewOffset);

private:
    /// Everything the painter keeps for one output. The model half is plain
    /// data; the texture half is GL-owned and only ever touched from paint()
    /// / releaseGl() (clearOutput/clearAll move it to the graveyard).
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
        /// style or the scale: the whole union is re-rasterised.
        bool dirty = true;
        /// Indicator rects whose hover state changed since the last paint.
        /// Consumed by paint() as texture sub-updates when the texture is
        /// otherwise current; ignored (the full raster covers them) when
        /// `dirty` is set.
        QVector<QRect> hoverDirtyRects;
        /// Whether the last completed pass blitted this output's pills.
        bool paintedLastPass = false;
        std::unique_ptr<KWin::GLTexture> texture;
        /// Bounds and device-pixel ratio the live texture was rasterised
        /// for. A change in either is a re-rasterise, not just a re-upload.
        QRect textureBounds;
        qreal textureScale = 0.0;
        /// Bounds/scale pair for which rasterising or uploading FAILED (an
        /// image too large for GL, an allocation failure). While it matches
        /// the current pair the paint does not retry every frame; any model,
        /// style or scale change clears it.
        QRect failedBounds;
        qreal failedScale = 0.0;
    };

    PerOutput* find(KWin::LogicalOutput* output);
    const PerOutput* find(KWin::LogicalOutput* output) const;

    /// Recompute `hits` and `bounds` from the model, and drop a hover whose
    /// window is no longer on the strip.
    static void rebuildLayout(PerOutput& entry);
    /// The indicator rect containing @p windowId's pill, or a null rect.
    static QRect indicatorRectFor(const PerOutput& entry, const QString& windowId);
    /// Retire @p entry's texture to the graveyard (no GL call).
    void retireTexture(PerOutput& entry);
    /// Delete retired textures; requires a current context.
    void drainRetired();

    std::unordered_map<KWin::LogicalOutput*, PerOutput> m_outputs;
    /// Textures whose output/model was dropped off-context, awaiting the
    /// next GL-current point (paint() or releaseGl()) for deletion.
    std::vector<std::unique_ptr<KWin::GLTexture>> m_retiredTextures;
    /// GL_MAX_TEXTURE_SIZE, queried once on the first paint (0 = unknown).
    int m_maxTextureSize = 0;
};

} // namespace PlasmaZones
