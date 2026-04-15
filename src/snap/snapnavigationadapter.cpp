// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapnavigationadapter.h"

#include "../dbus/windowtrackingadaptor.h"

namespace PlasmaZones {

SnapNavigationAdapter::SnapNavigationAdapter(WindowTrackingAdaptor* adaptor)
    : m_adaptor(adaptor)
{
}

void SnapNavigationAdapter::focusInDirection(const QString& direction, const QString& /*screenId*/)
{
    if (m_adaptor) {
        m_adaptor->focusAdjacentZone(direction);
    }
}

void SnapNavigationAdapter::moveFocusedInDirection(const QString& direction, const QString& /*screenId*/)
{
    if (m_adaptor) {
        m_adaptor->moveWindowToAdjacentZone(direction);
    }
}

void SnapNavigationAdapter::swapFocusedInDirection(const QString& direction, const QString& /*screenId*/)
{
    if (m_adaptor) {
        m_adaptor->swapWindowWithAdjacentZone(direction);
    }
}

void SnapNavigationAdapter::moveFocusedToPosition(int position, const QString& screenId)
{
    if (m_adaptor) {
        m_adaptor->snapToZoneByNumber(position, screenId);
    }
}

void SnapNavigationAdapter::rotateWindows(bool clockwise, const QString& screenId)
{
    if (m_adaptor) {
        m_adaptor->rotateWindowsInLayout(clockwise, screenId);
    }
}

void SnapNavigationAdapter::reapplyLayout(const QString& /*screenId*/)
{
    if (m_adaptor) {
        m_adaptor->resnapToNewLayout();
    }
}

void SnapNavigationAdapter::snapAllWindows(const QString& screenId)
{
    if (m_adaptor) {
        m_adaptor->snapAllWindows(screenId);
    }
}

void SnapNavigationAdapter::toggleFocusedFloat(const QString& /*screenId*/)
{
    if (m_adaptor) {
        m_adaptor->toggleWindowFloat();
    }
}

void SnapNavigationAdapter::cycleFocus(bool forward, const QString& /*screenId*/)
{
    if (m_adaptor) {
        m_adaptor->cycleWindowsInZone(forward);
    }
}

void SnapNavigationAdapter::pushToEmptyZone(const QString& screenId)
{
    if (m_adaptor) {
        m_adaptor->pushToEmptyZone(screenId);
    }
}

void SnapNavigationAdapter::restoreFocusedWindow(const QString& /*screenId*/)
{
    if (m_adaptor) {
        m_adaptor->restoreWindowSize();
    }
}

} // namespace PlasmaZones
