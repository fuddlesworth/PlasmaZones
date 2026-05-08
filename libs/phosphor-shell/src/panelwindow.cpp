// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PanelWindow.h>

namespace PhosphorShell {

PanelWindow::PanelWindow(QQuickItem* parent)
    : QQuickItem(parent)
{
}

PanelWindow::~PanelWindow() = default;

PanelWindow::Edge PanelWindow::edge() const
{
    return m_edge;
}

void PanelWindow::setEdge(Edge edge)
{
    if (m_edge == edge) {
        return;
    }
    m_edge = edge;
    Q_EMIT edgeChanged();
}

int PanelWindow::thickness() const
{
    return m_thickness;
}

void PanelWindow::setThickness(int thickness)
{
    if (m_thickness == thickness) {
        return;
    }
    m_thickness = thickness;
    Q_EMIT thicknessChanged();
}

QScreen* PanelWindow::screen() const
{
    return m_screen;
}

void PanelWindow::setScreen(QScreen* screen)
{
    if (m_screen == screen) {
        return;
    }
    m_screen = screen;
    Q_EMIT screenChanged();
}

PanelWindow::Layer PanelWindow::layer() const
{
    return m_layer;
}

void PanelWindow::setLayer(Layer layer)
{
    if (m_layer == layer) {
        return;
    }
    m_layer = layer;
    Q_EMIT layerChanged();
}

int PanelWindow::exclusiveZone() const
{
    return m_exclusiveZone;
}

void PanelWindow::setExclusiveZone(int zone)
{
    if (m_exclusiveZone == zone) {
        return;
    }
    m_exclusiveZone = zone;
    Q_EMIT exclusiveZoneChanged();
}

bool PanelWindow::exclusiveZoneEnabled() const
{
    return m_exclusiveZoneEnabled;
}

void PanelWindow::setExclusiveZoneEnabled(bool enabled)
{
    if (m_exclusiveZoneEnabled == enabled) {
        return;
    }
    m_exclusiveZoneEnabled = enabled;
    Q_EMIT exclusiveZoneEnabledChanged();
}

} // namespace PhosphorShell
