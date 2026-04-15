// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapnavigationadapter.h"

#include "SnapEngine.h"

namespace PlasmaZones {

SnapNavigationAdapter::SnapNavigationAdapter(SnapEngine* engine)
    : m_engine(engine)
{
}

void SnapNavigationAdapter::focusInDirection(const QString& direction, const QString& /*screenId*/)
{
    if (m_engine) {
        m_engine->focusInDirection(direction);
    }
}

void SnapNavigationAdapter::moveFocusedInDirection(const QString& direction, const QString& /*screenId*/)
{
    if (m_engine) {
        m_engine->moveFocusedInDirection(direction);
    }
}

void SnapNavigationAdapter::swapFocusedInDirection(const QString& direction, const QString& /*screenId*/)
{
    if (m_engine) {
        m_engine->swapFocusedInDirection(direction);
    }
}

void SnapNavigationAdapter::moveFocusedToPosition(int position, const QString& screenId)
{
    if (m_engine) {
        m_engine->moveFocusedToPosition(position, screenId);
    }
}

void SnapNavigationAdapter::rotateWindows(bool clockwise, const QString& screenId)
{
    if (m_engine) {
        m_engine->rotateWindowsInLayout(clockwise, screenId);
    }
}

void SnapNavigationAdapter::reapplyLayout(const QString& /*screenId*/)
{
    if (m_engine) {
        m_engine->resnapToNewLayout();
    }
}

void SnapNavigationAdapter::snapAllWindows(const QString& screenId)
{
    if (m_engine) {
        m_engine->snapAllWindows(screenId);
    }
}

void SnapNavigationAdapter::toggleFocusedFloat(const QString& /*screenId*/)
{
    if (m_engine) {
        m_engine->toggleFocusedFloat();
    }
}

void SnapNavigationAdapter::cycleFocus(bool forward, const QString& /*screenId*/)
{
    if (m_engine) {
        m_engine->cycleFocus(forward);
    }
}

void SnapNavigationAdapter::pushToEmptyZone(const QString& screenId)
{
    if (m_engine) {
        m_engine->pushFocusedToEmptyZone(screenId);
    }
}

void SnapNavigationAdapter::restoreFocusedWindow(const QString& /*screenId*/)
{
    if (m_engine) {
        m_engine->restoreFocusedWindow();
    }
}

} // namespace PlasmaZones
