// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QScreen;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT PanelWindow : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PanelWindow)

    Q_PROPERTY(Edge edge READ edge WRITE setEdge NOTIFY edgeChanged)
    Q_PROPERTY(int thickness READ thickness WRITE setThickness NOTIFY thicknessChanged)
    Q_PROPERTY(QScreen* screen READ screen WRITE setScreen NOTIFY screenChanged)
    Q_PROPERTY(Layer layer READ layer WRITE setLayer NOTIFY layerChanged)
    Q_PROPERTY(int exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged)
    Q_PROPERTY(bool exclusiveZoneEnabled READ exclusiveZoneEnabled WRITE setExclusiveZoneEnabled NOTIFY
                   exclusiveZoneEnabledChanged)

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

    explicit PanelWindow(QQuickItem* parent = nullptr);
    ~PanelWindow() override;

    Edge edge() const;
    void setEdge(Edge edge);

    int thickness() const;
    void setThickness(int thickness);

    QScreen* screen() const;
    void setScreen(QScreen* screen);

    Layer layer() const;
    void setLayer(Layer layer);

    int exclusiveZone() const;
    void setExclusiveZone(int zone);

    bool exclusiveZoneEnabled() const;
    void setExclusiveZoneEnabled(bool enabled);

Q_SIGNALS:
    void edgeChanged();
    void thicknessChanged();
    void screenChanged();
    void layerChanged();
    void exclusiveZoneChanged();
    void exclusiveZoneEnabledChanged();

private:
    Edge m_edge = Top;
    int m_thickness = 32;
    QScreen* m_screen = nullptr;
    Layer m_layer = LayerTop;
    int m_exclusiveZone = -1;
    bool m_exclusiveZoneEnabled = true;
};

} // namespace PhosphorShell
