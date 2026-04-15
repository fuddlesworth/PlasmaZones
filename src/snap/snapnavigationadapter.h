// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/inavigationactions.h"
#include "plasmazones_export.h"
#include <QString>

namespace PlasmaZones {

class WindowTrackingAdaptor;

/**
 * @brief Thin INavigationActions adapter for snap-mode navigation.
 *
 * Snap-mode navigation logic historically lives in WindowTrackingAdaptor
 * (focusAdjacentZone, moveWindowToAdjacentZone, etc.). This adapter
 * presents that logic under the INavigationActions interface so the
 * daemon's shortcut handlers can dispatch through ScreenModeRouter
 * without branching on mode.
 *
 * In a future refactor, the navigation logic should move out of
 * WindowTrackingAdaptor into SnapEngine (where it belongs — the D-Bus
 * adaptor should be a thin facade, not a partial engine). When that
 * happens, this adapter's implementation flips from forwarding to WTA
 * to forwarding to SnapEngine, without the daemon or the interface
 * needing to change.
 *
 * Parameter mapping:
 *   - `focusInDirection` → WTA::focusAdjacentZone
 *   - `moveFocusedInDirection` → WTA::moveWindowToAdjacentZone
 *   - `swapFocusedInDirection` → WTA::swapWindowWithAdjacentZone
 *   - `moveFocusedToPosition(pos, screen)` → WTA::snapToZoneByNumber(pos, screen)
 *   - `rotateWindows(cw, screen)` → WTA::rotateWindowsInLayout(cw, screen)
 *   - `reapplyLayout(screen)` → WTA::resnapToNewLayout()
 *                             (WTA's implementation is screen-agnostic)
 *   - `toggleFocusedFloat(screen)` → WTA::toggleWindowFloat()
 *   - `cycleFocus(forward, screen)` → WTA::cycleWindowsInZone(forward)
 */
class PLASMAZONES_EXPORT SnapNavigationAdapter : public INavigationActions
{
public:
    explicit SnapNavigationAdapter(WindowTrackingAdaptor* adaptor);
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
    WindowTrackingAdaptor* m_adaptor; // not owned
};

} // namespace PlasmaZones
