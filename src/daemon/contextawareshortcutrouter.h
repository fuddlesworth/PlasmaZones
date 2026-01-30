// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointer>

namespace PlasmaZones {

class AutotileEngine;
class ModeTracker;
class WindowTrackingAdaptor;

/**
 * @brief Routes keyboard shortcuts based on current tiling mode
 *
 * ContextAwareShortcutRouter enables shortcuts to behave differently
 * depending on whether the user is in Manual (zone-based) mode or
 * Autotile mode. This provides a consistent set of shortcuts that
 * "do the right thing" in either context.
 *
 * Shortcut behavior mapping:
 * - Meta+Alt+,/. : Manual=cycle windows in zone, Autotile=focusPrevious/focusNext
 * - Meta+Ctrl+[/]: Manual=rotate windows through zones, Autotile=rotate window order
 * - Meta+Alt+F   : Manual=toggle float (unsnap), Autotile=toggle float (exclude from tiling)
 *
 * Usage:
 * @code
 * auto *router = new ContextAwareShortcutRouter(modeTracker, autotileEngine,
 *                                                windowTrackingAdaptor, this);
 *
 * // Connect shortcuts to router instead of direct handlers
 * connect(shortcutManager, &ShortcutManager::cycleWindowsInZoneRequested,
 *         router, &ContextAwareShortcutRouter::cycleWindows);
 * @endcode
 */
class ContextAwareShortcutRouter : public QObject
{
    Q_OBJECT

public:
    explicit ContextAwareShortcutRouter(ModeTracker* modeTracker,
                                         AutotileEngine* autotileEngine,
                                         WindowTrackingAdaptor* windowTrackingAdaptor,
                                         QObject* parent = nullptr);
    ~ContextAwareShortcutRouter() override;

    /**
     * @brief Route cycle windows shortcut (Meta+Alt+,/.)
     *
     * Manual mode: Cycle focus through windows in the same zone
     * Autotile mode: Focus next/previous tiled window
     *
     * @param forward true for forward/next, false for backward/previous
     */
    Q_INVOKABLE void cycleWindows(bool forward);

    /**
     * @brief Route rotate windows shortcut (Meta+Ctrl+[/])
     *
     * Manual mode: Rotate all windows clockwise/counterclockwise through zones
     * Autotile mode: Rotate the window order in the tiling stack
     *
     * @param clockwise true for clockwise, false for counterclockwise
     */
    Q_INVOKABLE void rotateWindows(bool clockwise);

    /**
     * @brief Route toggle float shortcut (Meta+Alt+F)
     *
     * Manual mode: Unsnap the window from its zone (restore original size)
     * Autotile mode: Toggle the window between tiled and floating states
     */
    Q_INVOKABLE void toggleFloat();

private:
    QPointer<ModeTracker> m_modeTracker;
    QPointer<AutotileEngine> m_autotileEngine;
    QPointer<WindowTrackingAdaptor> m_windowTrackingAdaptor;
};

} // namespace PlasmaZones
