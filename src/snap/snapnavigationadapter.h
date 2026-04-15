// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/inavigationactions.h"
#include "plasmazones_export.h"
#include <QPointer>
#include <QString>

namespace PlasmaZones {

class SnapEngine;

/**
 * @brief Thin INavigationActions adapter for SnapEngine.
 *
 * Maps INavigationActions user-intent calls to SnapEngine's concrete
 * navigation methods. The NavigationContext is forwarded as-is —
 * SnapEngine's methods use both windowId and screenId explicitly
 * rather than reading them from the WTA shadow store.
 *
 * Parameter mapping:
 *   - focusInDirection        → engine->focusInDirection(dir, ctx)
 *   - moveFocusedInDirection  → engine->moveFocusedInDirection(dir, ctx)
 *   - swapFocusedInDirection  → engine->swapFocusedInDirection(dir, ctx)
 *   - moveFocusedToPosition   → engine->moveFocusedToPosition(pos, ctx)
 *   - rotateWindows           → engine->rotateWindowsInLayout(cw, ctx.screenId)
 *   - reapplyLayout           → engine->resnapToNewLayout()
 *   - snapAllWindows          → engine->snapAllWindows(ctx.screenId)
 *   - toggleFocusedFloat      → engine->toggleFocusedFloat(ctx)
 *   - cycleFocus              → engine->cycleFocus(forward, ctx)
 *   - pushToEmptyZone         → engine->pushFocusedToEmptyZone(ctx)
 *   - restoreFocusedWindow    → engine->restoreFocusedWindow(ctx)
 */
class PLASMAZONES_EXPORT SnapNavigationAdapter : public INavigationActions
{
public:
    explicit SnapNavigationAdapter(SnapEngine* engine);
    ~SnapNavigationAdapter() override = default;

    void focusInDirection(const QString& direction, const NavigationContext& ctx) override;
    void moveFocusedInDirection(const QString& direction, const NavigationContext& ctx) override;
    void swapFocusedInDirection(const QString& direction, const NavigationContext& ctx) override;
    void moveFocusedToPosition(int position, const NavigationContext& ctx) override;
    void rotateWindows(bool clockwise, const NavigationContext& ctx) override;
    void reapplyLayout(const NavigationContext& ctx) override;
    void snapAllWindows(const NavigationContext& ctx) override;
    void toggleFocusedFloat(const NavigationContext& ctx) override;
    void cycleFocus(bool forward, const NavigationContext& ctx) override;
    void pushToEmptyZone(const NavigationContext& ctx) override;
    void restoreFocusedWindow(const NavigationContext& ctx) override;

private:
    // QPointer — not owned, but auto-nulls on engine destruction during
    // shutdown. See AutotileNavigationAdapter::m_engine for rationale.
    QPointer<SnapEngine> m_engine;
};

} // namespace PlasmaZones
