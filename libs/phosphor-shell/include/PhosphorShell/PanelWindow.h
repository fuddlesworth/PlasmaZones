// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QMargins>
#include <QPointer>
#include <QQuickItem>
#include <QRect>
#include <QSize>

QT_BEGIN_NAMESPACE
class QScreen;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT PanelWindow : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(Edge edge READ edge WRITE setEdge NOTIFY edgeChanged)
    Q_PROPERTY(int thickness READ thickness WRITE setThickness NOTIFY thicknessChanged)
    /// Additional surface pixels rendered BEYOND the visible panel
    /// thickness, intended for the shell to draw a drop-shadow into.
    /// The layer-shell surface size = thickness + shadowSize on the
    /// edge-perpendicular axis; the exclusiveZone advertised to the
    /// compositor stays at `thickness` so other windows don't reserve
    /// the shadow area. The shader is responsible for actually
    /// rendering the shadow in that extra strip — PanelWindow only
    /// hands it the surface space.
    Q_PROPERTY(int shadowSize READ shadowSize WRITE setShadowSize NOTIFY shadowSizeChanged)
    /// Depth of the surface's INPUT REGION, when it should differ from
    /// `thickness`. 0 (the default) means "follow thickness", which is
    /// what every panel wants at rest.
    ///
    /// This exists for a surface that paints into its shadow strip and
    /// needs clicks there: a bar with a popout growing out of it into the
    /// space `shadowSize` reserved. The pocket is painted, so it must also
    /// be clickable, but only while it is open.
    ///
    /// Unlike `thickness`, this IS sampled live: ShellEngine re-applies the
    /// input region whenever it changes. That is deliberately the one
    /// post-materialization geometry write a panel may make. It is safe
    /// precisely because it moves nothing else — the surface size, the
    /// anchors and the exclusive zone all stay frozen at their
    /// materialization values, so a window tiled below the bar does not
    /// shift when a popout opens. Widening `thickness` instead would move
    /// the exclusive zone and shove every tiled window down the screen.
    ///
    /// Clamped by visibleBand() to the surface, so a value past
    /// `thickness + shadowSize` simply makes the whole surface interactive
    /// rather than producing a region that overhangs it.
    Q_PROPERTY(int interactiveThickness READ interactiveThickness WRITE setInteractiveThickness NOTIFY
                   interactiveThicknessChanged)
    /// Radius (logical pixels) of the concave quarter-arc carved into
    /// each corner of the visible panel where the panel meets the
    /// desktop area. 0 disables the carve. Larger values eat further
    /// into the panel, so reasonable choices are roughly 1/4 .. 1/3
    /// of `thickness`. The carve is rendered by the shader as an
    /// alpha cutout — the panel's wl_surface remains rectangular and
    /// the compositor blends whatever's behind the carved region
    /// (wallpaper / other windows) into the cutout pixels, giving
    /// the "desktop flows around the panel" look popularised by
    /// Quickshell/Noctalia shells. Which corners are carved depends
    /// on the panel's edge: Top → bottom-left + bottom-right; the
    /// shader is responsible for matching the panel's orientation.
    ///
    /// The carve is VISUAL ONLY. `visibleBand` — and therefore the surface's
    /// input region — is the full rectangle, so a carved corner shows the
    /// wallpaper or window behind it while still consuming clicks. Sharpening
    /// the region to match would mean subtracting an ellipse quadrant per
    /// carved corner; it is not done because the region is also what makes
    /// the shadow strip click-through, and the two carve quadrants sit
    /// inside the exclusive zone where no window is placed. A consumer that
    /// carves aggressively enough for that to matter should expect clicks in
    /// the carve to hit the panel.
    Q_PROPERTY(int cornerCarveRadius READ cornerCarveRadius WRITE setCornerCarveRadius NOTIFY cornerCarveRadiusChanged)
    Q_PROPERTY(QScreen* screen READ screen WRITE setScreen NOTIFY screenChanged)
    // `panelLayer` rather than `layer` — QQuickItem already exposes a
    // FINAL `layer` group property (the cached-rendering layer accessed
    // as `Item.layer.enabled`), and shadowing it triggered a
    // `qt.qml.propertyCache: Final member layer is overridden` warning
    // on every panel construction. Renaming avoids the collision.
    Q_PROPERTY(Layer panelLayer READ panelLayer WRITE setPanelLayer NOTIFY panelLayerChanged)
    Q_PROPERTY(int exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged)
    Q_PROPERTY(bool exclusiveZoneEnabled READ exclusiveZoneEnabled WRITE setExclusiveZoneEnabled NOTIFY
                   exclusiveZoneEnabledChanged)
    Q_PROPERTY(Alignment alignment READ alignment WRITE setAlignment NOTIFY alignmentChanged)
    Q_PROPERTY(int panelLength READ panelLength WRITE setPanelLength NOTIFY panelLengthChanged)
    Q_PROPERTY(QMargins margins READ margins WRITE setMargins NOTIFY marginsChanged)
    // Layer-shell keyboard_interactivity. Default `None` matches what
    // Plasma's panel ships with: panels grab pointer events for their
    // own widgets but never keyboard focus from other windows. Clicking
    // a tray icon shouldn't steal focus from the user's terminal /
    // editor / browser. Set to `OnDemand` when the panel hosts an
    // input field (search launcher etc.) that needs key events; set
    // to `Exclusive` for shell-modal surfaces like a lock screen.
    // Popups attached to the panel get their own xdg_popup grab and
    // can receive keyboard input independently of this setting.
    Q_PROPERTY(KeyboardFocus keyboardFocus READ keyboardFocus WRITE setKeyboardFocus NOTIFY keyboardFocusChanged)

public:
    enum Edge {
        Top,
        Bottom,
        Left,
        Right,
    };
    Q_ENUM(Edge)

    enum Layer {
        LayerBackground,
        LayerBottom,
        LayerTop,
        LayerOverlay,
    };
    Q_ENUM(Layer)

    enum Alignment {
        Fill,
        Start,
        Center,
        End,
    };
    Q_ENUM(Alignment)

    // Mirror of PhosphorLayer::KeyboardInteractivity. Kept in this
    // namespace so QML doesn't need to import PhosphorLayer just to
    // name the enumerators. ShellEngine maps these to the layer
    // library's enum at surface-creation time.
    enum KeyboardFocus {
        None, ///< Never receives keyboard focus (default — typical panel).
        OnDemand, ///< Receives focus when the user clicks the surface.
        Exclusive ///< Holds focus exclusively; for lock screens etc.
    };
    Q_ENUM(KeyboardFocus)

    explicit PanelWindow(QQuickItem* parent = nullptr);
    ~PanelWindow() override;

    /// The painted band of a panel surface of `surfaceSize`, in
    /// surface-local device-independent coordinates: the `thickness` slice
    /// anchored to `edge`, excluding the `shadowSize` strip beyond it.
    ///
    /// This is what the surface's input region should be set to. Without
    /// one the shadow strip is transparent but still accepts pointer
    /// events, so it swallows clicks along the edge of whatever tiles
    /// beneath the panel — and because the advertised exclusiveZone is
    /// `thickness`, windows land exactly where the strip overlaps them.
    ///
    /// Static and pure so the arithmetic can be tested without a live
    /// Wayland surface: which edge anchors where, and what happens when
    /// thickness meets or exceeds the surface depth, is the part that can
    /// be wrong. Returns a null QRect for a degenerate surface or a
    /// non-positive thickness, which callers treat as "apply nothing".
    [[nodiscard]] static QRect visibleBand(Edge edge, int thickness, QSize surfaceSize);

    [[nodiscard]] Edge edge() const;
    void setEdge(Edge edge);

    [[nodiscard]] int thickness() const;
    void setThickness(int thickness);

    [[nodiscard]] int shadowSize() const;
    void setShadowSize(int size);

    [[nodiscard]] int interactiveThickness() const;
    void setInteractiveThickness(int thickness);

    /// The input-region depth to actually apply: `interactiveThickness`
    /// when set, otherwise `thickness`. One place so the "0 means follow
    /// thickness" rule cannot be spelled differently by two callers.
    ///
    /// Note WHICH of the two inputs a caller should expect to be live.
    /// `interactiveThickness` is designed to change after materialization
    /// (the bar's socket animates it) and ShellEngine re-applies the input
    /// region when it does. `thickness` is not: the surface size, anchors
    /// and exclusive zone are all fixed at materialization, so a panel that
    /// changes `thickness` afterwards is already inconsistent with its own
    /// surface, and the depth this returns will follow the new value on the
    /// next re-apply. Treat a post-materialization `thickness` write as
    /// unsupported rather than as a supported live property.
    [[nodiscard]] int effectiveInputThickness() const;

    [[nodiscard]] int cornerCarveRadius() const;
    void setCornerCarveRadius(int radius);

    [[nodiscard]] QScreen* screen() const;
    void setScreen(QScreen* screen);

    [[nodiscard]] Layer panelLayer() const;
    void setPanelLayer(Layer layer);

    [[nodiscard]] int exclusiveZone() const;
    void setExclusiveZone(int zone);

    [[nodiscard]] bool exclusiveZoneEnabled() const;
    void setExclusiveZoneEnabled(bool enabled);

    [[nodiscard]] Alignment alignment() const;
    void setAlignment(Alignment alignment);

    [[nodiscard]] int panelLength() const;
    void setPanelLength(int length);

    [[nodiscard]] QMargins margins() const;
    void setMargins(const QMargins& margins);

    [[nodiscard]] KeyboardFocus keyboardFocus() const;
    void setKeyboardFocus(KeyboardFocus focus);

Q_SIGNALS:
    void edgeChanged();
    void thicknessChanged();
    void shadowSizeChanged();
    void interactiveThicknessChanged();
    void cornerCarveRadiusChanged();
    void screenChanged();
    void panelLayerChanged();
    void exclusiveZoneChanged();
    void exclusiveZoneEnabledChanged();
    void alignmentChanged();
    void panelLengthChanged();
    void marginsChanged();
    void keyboardFocusChanged();

private:
    Edge m_edge = Top;
    int m_thickness = 32;
    int m_shadowSize = 0;
    // 0 = follow m_thickness. See the property docs above.
    int m_interactiveThickness = 0;
    int m_cornerCarveRadius = 0;
    // QPointer so monitor hot-unplug doesn't leave us with a dangling
    // QScreen pointer. ScreenManager owns QScreen lifetimes externally.
    QPointer<QScreen> m_screen;
    Layer m_layer = LayerTop;
    // -1 sentinel: "no explicit override, derive from alignment"
    //  0 sentinel: "auto-fit to content via implicitWidth/Height"
    // >0:         "explicit pin in screen-axis pixels"
    int m_exclusiveZone = -1;
    bool m_exclusiveZoneEnabled = true;
    Alignment m_alignment = Fill;
    // -1 sentinel: "fill the screen-aligned axis (Fill alignment)"
    //  0 sentinel: "auto-fit to root item's implicitWidth/Height"
    // >0:         "explicit pin in screen-axis pixels"
    int m_panelLength = -1;
    QMargins m_margins;
    KeyboardFocus m_keyboardFocus = None;
};

} // namespace PhosphorShell
