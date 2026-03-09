// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "macros.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/screenmanager.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../../autotile/AutotileEngine.h"
#include "../../autotile/AlgorithmRegistry.h"
#include "../../snap/SnapEngine.h"
#include "../../core/iwindowengine.h"
#include "../modetracker.h"
#include <QScreen>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Engine routing
// ═══════════════════════════════════════════════════════════════════════════════

IWindowEngine* Daemon::engineForScreen(const QString& screenName) const
{
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screenName)) {
        return m_autotileEngine.get();
    }
    if (m_snapEngine) {
        return m_snapEngine.get();
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation handlers — single code path per operation (DRY/SOLID)
// Resolve screen → check mode (autotile vs zones) → delegate → OSD from backend
// ═══════════════════════════════════════════════════════════════════════════════

void Daemon::handleRotate(bool clockwise)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        qCDebug(lcDaemon) << "No screen info for rotate shortcut — skipping";
        return;
    }
    if (auto* engine = engineForScreen(screen->name())) {
        engine->rotateWindows(clockwise, screen->name());
    }
}

void Daemon::handleFloat()
{
    // Delegate to WTA → effect → unified toggleFloatForWindow.
    // The effect resolves the active KWin window + screen, stores both pre-snap
    // and pre-autotile geometry, then calls toggleFloatForWindow which the daemon
    // routes internally to snapping toggle or autotile engine based on screen mode.
    if (!m_windowTrackingAdaptor) {
        return;
    }
    m_windowTrackingAdaptor->toggleWindowFloat();
}

void Daemon::handleMove(NavigationDirection direction)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        return;
    }
    QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown move navigation direction:" << static_cast<int>(direction);
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        m_autotileEngine->swapFocusedInDirection(dirStr);
    } else if (m_snapEngine) {
        m_snapEngine->moveInDirection(dirStr);
    }
}

void Daemon::handleFocus(NavigationDirection direction)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        return;
    }
    QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown focus navigation direction:" << static_cast<int>(direction);
        return;
    }
    if (auto* engine = engineForScreen(screen->name())) {
        engine->focusInDirection(dirStr, QStringLiteral("focus"));
    }
}

void Daemon::handlePush()
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        qCDebug(lcDaemon) << "No screen info for pushToEmptyZone shortcut — skipping";
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        return;
    }
    if (m_snapEngine) {
        m_snapEngine->pushToEmptyZone(screen->name());
    }
}

void Daemon::handleRestore()
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        // In autotile mode, "restore" floats the window (equivalent to unsnapping)
        m_autotileEngine->toggleFocusedWindowFloat();
        return;
    }
    m_windowTrackingAdaptor->restoreWindowSize();
}

void Daemon::handleSwap(NavigationDirection direction)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        return;
    }
    QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown swap navigation direction:" << static_cast<int>(direction);
        return;
    }
    if (auto* engine = engineForScreen(screen->name())) {
        engine->swapInDirection(dirStr, QStringLiteral("swap"));
    }
}

void Daemon::handleSnap(int zoneNumber)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        qCDebug(lcDaemon) << "No screen info for snapToZone shortcut — skipping";
        return;
    }
    if (auto* engine = engineForScreen(screen->name())) {
        engine->moveToPosition(QString(), zoneNumber, screen->name());
    }
}

void Daemon::handleCycle(bool forward)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        QString dirStr = forward ? QStringLiteral("right") : QStringLiteral("left");
        m_autotileEngine->focusInDirection(dirStr, QStringLiteral("cycle"));
    } else if (m_snapEngine) {
        m_snapEngine->cycleWindowsInZone(forward);
    }
}

void Daemon::handleResnap()
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        m_autotileEngine->retile(screen->name());
    } else if (m_snapEngine) {
        m_snapEngine->resnapToNewLayout();
    }
}

void Daemon::handleSnapAll()
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        qCDebug(lcDaemon) << "No screen info for snapAllWindows shortcut — skipping";
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        m_autotileEngine->retile(screen->name());
    } else if (m_snapEngine) {
        m_snapEngine->snapAllWindows(screen->name());
    }
}

// DRY macro invocations for identical autotile-only handlers
HANDLE_AUTOTILE_ONLY(FocusMaster, focusMaster())
HANDLE_AUTOTILE_ONLY(SwapWithMaster, swapFocusedWithMaster())
HANDLE_AUTOTILE_ONLY(IncreaseMasterRatio, increaseMasterRatio())
HANDLE_AUTOTILE_ONLY(DecreaseMasterRatio, decreaseMasterRatio())
HANDLE_AUTOTILE_ONLY(IncreaseMasterCount, increaseMasterCount())
HANDLE_AUTOTILE_ONLY(DecreaseMasterCount, decreaseMasterCount())

void Daemon::handleRetile()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    m_autotileEngine->retile();
    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        QString screenName = screen ? screen->name() : QString();
        if (screenName.isEmpty() && !m_autotileEngine->autotileScreens().isEmpty()) {
            screenName = *m_autotileEngine->autotileScreens().begin();
        }
        m_overlayService->showNavigationOsd(true, QStringLiteral("retile"), QStringLiteral("retiled"), QString(),
                                            QString(), screenName);
    }
}

void Daemon::resnapIfManualMode()
{
    // Only resnap for manual layouts — autotile handles its own retile.
    // Resnapping during autotile uses a stale buffer from the old snap-mode
    // layout, sending competing geometry D-Bus signals to the effect.
    if (m_snapEngine && !(m_modeTracker && m_modeTracker->isAutotileMode())) {
        m_suppressResnapOsd = 1;
        m_snapEngine->resnapToNewLayout();
    }
}

QString Daemon::resolveAlgorithmId() const
{
    if (m_modeTracker) {
        QString algoId = m_modeTracker->lastAutotileAlgorithm();
        if (!algoId.isEmpty()) {
            return algoId;
        }
    }
    if (m_settings) {
        QString algoId = m_settings->autotileAlgorithm();
        if (!algoId.isEmpty()) {
            return algoId;
        }
    }
    return AlgorithmRegistry::defaultAlgorithmId();
}

} // namespace PlasmaZones
