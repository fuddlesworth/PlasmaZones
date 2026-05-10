// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QMargins>
#include <QPointer>
#include <QQuickItem>

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
    Q_PROPERTY(int cornerCarveRadius READ cornerCarveRadius WRITE setCornerCarveRadius NOTIFY cornerCarveRadiusChanged)
    Q_PROPERTY(QScreen* screen READ screen WRITE setScreen NOTIFY screenChanged)
    Q_PROPERTY(Layer layer READ layer WRITE setLayer NOTIFY layerChanged)
    Q_PROPERTY(int exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged)
    Q_PROPERTY(bool exclusiveZoneEnabled READ exclusiveZoneEnabled WRITE setExclusiveZoneEnabled NOTIFY
                   exclusiveZoneEnabledChanged)
    Q_PROPERTY(Alignment alignment READ alignment WRITE setAlignment NOTIFY alignmentChanged)
    Q_PROPERTY(int panelLength READ panelLength WRITE setPanelLength NOTIFY panelLengthChanged)
    Q_PROPERTY(QMargins margins READ margins WRITE setMargins NOTIFY marginsChanged)

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

    explicit PanelWindow(QQuickItem* parent = nullptr);
    ~PanelWindow() override;

    [[nodiscard]] Edge edge() const;
    void setEdge(Edge edge);

    [[nodiscard]] int thickness() const;
    void setThickness(int thickness);

    [[nodiscard]] int shadowSize() const;
    void setShadowSize(int size);

    [[nodiscard]] int cornerCarveRadius() const;
    void setCornerCarveRadius(int radius);

    [[nodiscard]] QScreen* screen() const;
    void setScreen(QScreen* screen);

    [[nodiscard]] Layer layer() const;
    void setLayer(Layer layer);

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

Q_SIGNALS:
    void edgeChanged();
    void thicknessChanged();
    void shadowSizeChanged();
    void cornerCarveRadiusChanged();
    void screenChanged();
    void layerChanged();
    void exclusiveZoneChanged();
    void exclusiveZoneEnabledChanged();
    void alignmentChanged();
    void panelLengthChanged();
    void marginsChanged();

private:
    Edge m_edge = Top;
    int m_thickness = 32;
    int m_shadowSize = 0;
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
};

} // namespace PhosphorShell
