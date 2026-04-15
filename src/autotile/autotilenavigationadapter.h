// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/inavigationactions.h"
#include "plasmazones_export.h"
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
 * Parameter mapping:
 *   - `focusInDirection(dir, screen)` →
 *       engine->focusInDirection(dir, "focus")
 *   - `moveFocusedInDirection(dir, screen)` →
 *       engine->swapFocusedInDirection(dir, "move")
 *       (autotile's "move" IS swap-with-neighbour in the tiling order)
 *   - `swapFocusedInDirection(dir, screen)` →
 *       engine->swapFocusedInDirection(dir, "swap")
 *   - `moveFocusedToPosition(pos, screen)` →
 *       engine->moveFocusedToPosition(pos)
 *   - `rotateWindows(cw, screen)` →
 *       engine->rotateWindows(cw, screen)
 *   - `reapplyLayout(screen)` →
 *       engine->retile(screen)
 *   - `toggleFocusedFloat(screen)` →
 *       engine->toggleFocusedWindowFloat()
 *   - `cycleFocus(forward, screen)` →
 *       engine->focusInDirection(forward ? "right" : "left", "cycle")
 *
 * The `screen` parameter is intentionally ignored for autotile-side calls
 * that rely on the engine's internal focus tracking — by the time the
 * daemon invokes a navigation shortcut, the active screen has already
 * been resolved and the engine's focus state is the authoritative target.
 * Methods that legitimately scope by screen (rotateWindows, retile)
 * forward it through unchanged.
 */
class PLASMAZONES_EXPORT AutotileNavigationAdapter : public INavigationActions
{
public:
    explicit AutotileNavigationAdapter(AutotileEngine* engine);
    ~AutotileNavigationAdapter() override = default;

    void focusInDirection(const QString& direction, const QString& screenId) override;
    void moveFocusedInDirection(const QString& direction, const QString& screenId) override;
    void swapFocusedInDirection(const QString& direction, const QString& screenId) override;
    void moveFocusedToPosition(int position, const QString& screenId) override;
    void rotateWindows(bool clockwise, const QString& screenId) override;
    void reapplyLayout(const QString& screenId) override;
    void snapAllWindows(const QString& screenId) override;
    void toggleFocusedFloat(const QString& screenId) override;
    void cycleFocus(bool forward, const QString& screenId) override;
    void pushToEmptyZone(const QString& screenId) override;
    void restoreFocusedWindow(const QString& screenId) override;

private:
    AutotileEngine* m_engine; // not owned
};

} // namespace PlasmaZones
