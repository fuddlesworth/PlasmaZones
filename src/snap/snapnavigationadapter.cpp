// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapnavigationadapter.h"

#include "SnapEngine.h"

namespace PlasmaZones {

SnapNavigationAdapter::SnapNavigationAdapter(SnapEngine* engine)
    : m_engine(engine)
{
}

void SnapNavigationAdapter::focusInDirection(const QString& direction, const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->focusInDirection(direction, ctx);
    }
}

void SnapNavigationAdapter::moveFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->moveFocusedInDirection(direction, ctx);
    }
}

void SnapNavigationAdapter::swapFocusedInDirection(const QString& direction, const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->swapFocusedInDirection(direction, ctx);
    }
}

void SnapNavigationAdapter::moveFocusedToPosition(int position, const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->moveFocusedToPosition(position, ctx);
    }
}

void SnapNavigationAdapter::rotateWindows(bool clockwise, const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->rotateWindowsInLayout(clockwise, ctx.screenId);
    }
}

void SnapNavigationAdapter::reapplyLayout(const NavigationContext& /*ctx*/)
{
    if (m_engine) {
        m_engine->resnapToNewLayout();
    }
}

void SnapNavigationAdapter::snapAllWindows(const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->snapAllWindows(ctx.screenId);
    }
}

void SnapNavigationAdapter::toggleFocusedFloat(const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->toggleFocusedFloat(ctx);
    }
}

void SnapNavigationAdapter::cycleFocus(bool forward, const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->cycleFocus(forward, ctx);
    }
}

void SnapNavigationAdapter::pushToEmptyZone(const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->pushFocusedToEmptyZone(ctx);
    }
}

void SnapNavigationAdapter::restoreFocusedWindow(const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->restoreFocusedWindow(ctx);
    }
}

} // namespace PlasmaZones
