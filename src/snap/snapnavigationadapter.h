// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/inavigationactions.h"
#include "plasmazones_export.h"
#include <QString>

namespace PlasmaZones {

class SnapEngine;

/**
 * @brief Thin INavigationActions adapter for SnapEngine.
 *
 * Maps INavigationActions user-intent calls to SnapEngine's concrete
 * navigation methods. The screen parameter is forwarded where the engine
 * needs it and dropped where the engine resolves the target from its own
 * last-active-window shadow (via the WindowTrackingAdaptor back-ref).
 *
 * Parameter mapping:
 *   - `focusInDirection(dir, screen)` → engine->focusInDirection(dir)
 *   - `moveFocusedInDirection(dir, screen)` → engine->moveFocusedInDirection(dir)
 *   - `swapFocusedInDirection(dir, screen)` → engine->swapFocusedInDirection(dir)
 *   - `moveFocusedToPosition(pos, screen)` → engine->moveFocusedToPosition(pos, screen)
 *   - `rotateWindows(cw, screen)` → engine->rotateWindowsInLayout(cw, screen)
 *   - `reapplyLayout(screen)` → engine->resnapToNewLayout()
 *   - `snapAllWindows(screen)` → engine->snapAllWindows(screen)
 *   - `toggleFocusedFloat(screen)` → engine->toggleFocusedFloat()
 *   - `cycleFocus(forward, screen)` → engine->cycleFocus(forward)
 *   - `pushToEmptyZone(screen)` → engine->pushFocusedToEmptyZone(screen)
 *   - `restoreFocusedWindow(screen)` → engine->restoreFocusedWindow()
 */
class PLASMAZONES_EXPORT SnapNavigationAdapter : public INavigationActions
{
public:
    explicit SnapNavigationAdapter(SnapEngine* engine);
    ~SnapNavigationAdapter() override = default;

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
    SnapEngine* m_engine; // not owned
};

} // namespace PlasmaZones
