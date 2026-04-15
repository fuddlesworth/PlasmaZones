// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/inavigationactions.h"
#include "plasmazones_export.h"
#include <QPointer>
#include <QString>

namespace PlasmaZones {

class AutotileEngine;

/**
 * @brief Thin INavigationActions adapter for AutotileEngine.
 *
 * Maps the user-intent-shaped INavigationActions calls to AutotileEngine's
 * existing concrete methods so ScreenModeRouter can dispatch through a
 * common interface without the daemon branching on mode.
 *
 * Autotile semantics note: most adapter methods ignore the windowId field
 * of the NavigationContext. Autotile's internal focus tracking is the
 * authoritative target — by the time the daemon invokes a shortcut, the
 * engine already knows which window is focused, and the adapter forwards
 * calls like focusInDirection("left") without naming the window. Methods
 * that legitimately scope by screen (rotateWindows, retile) use the
 * screenId field.
 *
 * Parameter mapping:
 *   - focusInDirection        → engine->focusInDirection(dir, "focus")
 *   - moveFocusedInDirection  → engine->swapFocusedInDirection(dir, "move")
 *     (autotile's "move" IS swap-with-neighbour in the tiling order)
 *   - swapFocusedInDirection  → engine->swapFocusedInDirection(dir, "swap")
 *   - moveFocusedToPosition   → engine->moveFocusedToPosition(pos)
 *   - rotateWindows           → engine->rotateWindows(cw, ctx.screenId)
 *   - reapplyLayout           → engine->retile(ctx.screenId)
 *   - toggleFocusedFloat      → engine->toggleFocusedWindowFloat()
 *   - cycleFocus              → engine->focusInDirection(dir, "cycle")
 *   - pushToEmptyZone         → no-op (no empty zones in autotile)
 *   - restoreFocusedWindow    → engine->toggleFocusedWindowFloat()
 */
class PLASMAZONES_EXPORT AutotileNavigationAdapter : public INavigationActions
{
public:
    explicit AutotileNavigationAdapter(AutotileEngine* engine);
    ~AutotileNavigationAdapter() override = default;

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
    // QPointer — not owned, but auto-nulls if the engine is destroyed during
    // daemon shutdown before the adapter is torn down. Every method checks
    // the pointer before dispatching, so a dangling engine pointer becomes
    // a silent no-op instead of a use-after-free crash. Mirrors
    // SnapEngine::m_wta's pattern.
    QPointer<AutotileEngine> m_engine;
};

} // namespace PlasmaZones
