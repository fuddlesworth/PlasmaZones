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
