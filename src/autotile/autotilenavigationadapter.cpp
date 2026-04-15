// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotilenavigationadapter.h"

#include "AutotileEngine.h"

namespace PlasmaZones {

AutotileNavigationAdapter::AutotileNavigationAdapter(AutotileEngine* engine)
    : m_engine(engine)
{
}

void AutotileNavigationAdapter::focusInDirection(const QString& direction, const NavigationContext& /*ctx*/)
{
    if (m_engine) {
        m_engine->focusInDirection(direction, QStringLiteral("focus"));
    }
}

void AutotileNavigationAdapter::moveFocusedInDirection(const QString& direction, const NavigationContext& /*ctx*/)
{
    // In autotile, "move in direction" is implemented as swap-with-neighbour
    // in the tiling order — the only way to move is to trade places with
    // the neighbour. OSD label "move" keeps the user-facing wording.
    if (m_engine) {
        m_engine->swapFocusedInDirection(direction, QStringLiteral("move"));
    }
}

void AutotileNavigationAdapter::swapFocusedInDirection(const QString& direction, const NavigationContext& /*ctx*/)
{
    if (m_engine) {
        m_engine->swapFocusedInDirection(direction, QStringLiteral("swap"));
    }
}

void AutotileNavigationAdapter::moveFocusedToPosition(int position, const NavigationContext& /*ctx*/)
{
    if (m_engine) {
        m_engine->moveFocusedToPosition(position);
    }
}

void AutotileNavigationAdapter::rotateWindows(bool clockwise, const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->rotateWindows(clockwise, ctx.screenId);
    }
}

void AutotileNavigationAdapter::reapplyLayout(const NavigationContext& ctx)
{
    if (m_engine) {
        m_engine->retile(ctx.screenId);
    }
}

void AutotileNavigationAdapter::snapAllWindows(const NavigationContext& ctx)
{
    // Autotile has no distinct "snap all" — retile picks up every window
    // the engine is tracking and inserts any new ones into the layout.
    if (m_engine) {
        m_engine->retile(ctx.screenId);
    }
}

void AutotileNavigationAdapter::toggleFocusedFloat(const NavigationContext& /*ctx*/)
{
    if (m_engine) {
        m_engine->toggleFocusedWindowFloat();
    }
}

void AutotileNavigationAdapter::cycleFocus(bool forward, const NavigationContext& /*ctx*/)
{
    if (m_engine) {
        const QString dir = forward ? QStringLiteral("right") : QStringLiteral("left");
        m_engine->focusInDirection(dir, QStringLiteral("cycle"));
    }
}

void AutotileNavigationAdapter::pushToEmptyZone(const NavigationContext& /*ctx*/)
{
    // Autotile has no concept of empty zones — every tracked window is
    // placed by the layout algorithm. Deliberate no-op so the shortcut
    // becomes a harmless press in autotile mode rather than the daemon
    // having to branch on mode at the entry point.
}

void AutotileNavigationAdapter::restoreFocusedWindow(const NavigationContext& /*ctx*/)
{
    // "Restore" in autotile means pulling the focused window out of the
    // tiling layout — toggling its float state achieves exactly that.
    if (m_engine) {
        m_engine->toggleFocusedWindowFloat();
    }
}

} // namespace PlasmaZones
