// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../autotilehandler.h"
#include "../navigationhandler.h"
#include "../plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effectwindow.h>
#include <window.h>

#include <QLoggingCategory>
#include <QStringList>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

bool AutotileHandler::transferPreAutotileGeometry(const QString& windowId, const QString& fromScreenId,
                                                  const QString& toScreenId)
{
    auto fromIt = m_preAutotileGeometries.find(fromScreenId);
    if (fromIt == m_preAutotileGeometries.end()) {
        return false;
    }
    const QString savedKey = AutotileStateHelpers::findSavedGeometryKey(fromIt.value(), windowId);
    if (savedKey.isEmpty()) {
        return false;
    }
    QRectF geo = fromIt->take(savedKey);
    if (fromIt->isEmpty()) {
        m_preAutotileGeometries.erase(fromIt);
    }
    if (!geo.isValid()) {
        return false;
    }
    m_preAutotileGeometries[toScreenId][savedKey] = geo;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Monocle helpers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::unmaximizeMonocleWindow(const QString& windowId)
{
    if (!m_monocleMaximizedWindows.remove(windowId)) {
        return;
    }
    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    if (!w) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    ++m_suppressMaximizeChanged;
    kw->maximize(KWin::MaximizeRestore);
    --m_suppressMaximizeChanged;
}

void AutotileHandler::restoreAllMonocleMaximized()
{
    if (m_monocleMaximizedWindows.isEmpty()) {
        return;
    }
    ++m_suppressMaximizeChanged;
    for (const QString& wid : std::as_const(m_monocleMaximizedWindows)) {
        KWin::EffectWindow* w = m_effect->findWindowById(wid);
        if (w) {
            KWin::Window* kw = w->window();
            if (kw) {
                kw->maximize(KWin::MaximizeRestore);
            }
        }
    }
    --m_suppressMaximizeChanged;
    m_monocleMaximizedWindows.clear();
}

void AutotileHandler::restoreAllBorderless()
{
    // Snapshot first (setWindowBorderless mutates the buckets under us).
    const auto pairs = AutotileStateHelpers::allBorderlessPairs(m_border);
    for (const auto& p : pairs) {
        KWin::EffectWindow* w = m_effect->findWindowById(p.first);
        if (w) {
            setWindowBorderless(w, p.first, false, p.second);
        }
    }
    // Clear all border tracking (orphans included)
    m_border.borderlessWindowsByScreen.clear();
    m_border.tiledWindowsByScreen.clear();
    m_border.zoneGeometries.clear();
}

void AutotileHandler::updateHideTitleBarsSetting(bool enabled)
{
    const bool wasEnabled = m_border.hideTitleBars;
    m_border.hideTitleBars = enabled;
    if (wasEnabled && !enabled) {
        // Turning OFF — restore title bars for all borderless windows
        m_border.zoneGeometries.clear();
        const auto pairs = AutotileStateHelpers::allBorderlessPairs(m_border);
        for (const auto& p : pairs) {
            KWin::EffectWindow* win = m_effect->findWindowById(p.first);
            if (win) {
                setWindowBorderless(win, p.first, false, p.second);
            }
        }
    } else if (!wasEnabled && enabled) {
        // Turning ON — hide title bars for all currently tiled windows
        const auto pairs = AutotileStateHelpers::allTiledPairs(m_border);
        for (const auto& p : pairs) {
            KWin::EffectWindow* win = m_effect->findWindowById(p.first);
            if (win) {
                setWindowBorderless(win, p.first, true, p.second);
            }
        }
    }
}

void AutotileHandler::updateShowBorderSetting(bool enabled)
{
    m_border.showBorder = enabled;
}

// setFocusFollowsMouse(bool) is provided by TilingHandlerBase.

bool AutotileHandler::saveAndRecordPreAutotileGeometry(const QString& windowId, const QString& screenId,
                                                       const QRectF& frame, bool knownFreeFloating)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return false;
    }
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        return false;
    }
    auto& screenGeometries = m_preAutotileGeometries[screenId];
    // Use EXACT windowId match only — NOT stableId fallback.
    // Multiple instances of the same app (e.g., 3 Dolphin windows) share a stableId.
    // hasSavedGeometryForWindow's stableId fallback would return true after the first
    // instance is saved, preventing all other instances from saving their own geometry.
    // On restore, all instances would get the first instance's geometry — scrambling
    // window positions on every autotile ↔ snapping toggle.
    if (screenGeometries.contains(windowId)) {
        return false;
    }
    // Only save geometry for floating windows — snapped/tiled windows have zone
    // dimensions in frameGeometry(), not the original free-floating size. Storing
    // zone geometry here would cause handleDragToFloat to restore to zone size.
    //
    // EXCEPTION: freshly-opened windows are not tracked in the FloatingCache yet,
    // so isWindowFloating() returns false even though their frame IS the authoritative
    // free-floating spawn geometry. Callers that know they are processing a fresh
    // window pass knownFreeFloating=true to bypass the guard. Without that bypass,
    // the save is silently dropped and every later float-restore for this window
    // falls through to stale cross-session data (or, with exact-only lookups, nothing).
    if (!knownFreeFloating && !m_effect->isWindowFloating(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for snapped window" << windowId << "on" << screenId;
        return true;
    }
    screenGeometries[windowId] = frame;
    qCDebug(lcEffect) << "Saved pre-autotile geometry for" << windowId << "on" << screenId << ":" << frame;
    if (m_effect->m_daemonServiceRegistered) {
        // overwrite=true: the isWindowFloating() guard above already skipped
        // this path for snapped windows, so when we reach here the frame is
        // the window's authoritative free-floating geometry for THIS session.
        // The daemon persists pre-tile entries across window close/reopen
        // (keyed by appId for session restore), so a stale entry from a
        // prior session would otherwise block the fresh capture and leave
        // float-restore teleporting the window to ancient coordinates.
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("storePreTileGeometry"),
                                                       {windowId, qRound(frame.x()), qRound(frame.y()),
                                                        qRound(frame.width()), qRound(frame.height()), screenId, true},
                                                       QStringLiteral("storePreTileGeometry"));
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Borderless / title bar management
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::setWindowBorderless(KWin::EffectWindow* w, const QString& windowId, bool borderless,
                                          const QString& screenId)
{
    if (!w) {
        return;
    }
    if (screenId.isEmpty()) {
        qCWarning(lcEffect) << "setWindowBorderless: empty screenId for" << windowId << "(borderless=" << borderless
                            << ") — bucket tracking will drift";
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    if (borderless) {
        // Skip CSD windows (GTK/Electron) — hasDecoration() is false for them.
        if (!w->hasDecoration()) {
            return;
        }
        // Add to the screen-scoped bucket. "Was it already borderless
        // anywhere?" is what determines whether we call setNoBorder(true)
        // on the compositor — once the flag is set we don't re-set it.
        const bool wasBorderlessAnywhere = AutotileStateHelpers::isBorderlessWindow(m_border, windowId);
        AutotileStateHelpers::addBorderlessOnScreen(m_border, screenId, windowId);
        if (!wasBorderlessAnywhere) {
            kw->setNoBorder(true);
            qCDebug(lcEffect) << "Autotile: hid title bar for" << windowId << "(screen:" << screenId << ")";
        }
    } else {
        // Remove from the specific screen's bucket. Only actually restore
        // the title bar if the window is no longer tracked as borderless
        // on ANY screen — otherwise a sibling VS's retile still owns it.
        const bool removedHere = AutotileStateHelpers::removeBorderlessOnScreen(m_border, screenId, windowId);
        if (!removedHere) {
            return;
        }
        if (!AutotileStateHelpers::isBorderlessWindow(m_border, windowId)) {
            m_border.zoneGeometries.remove(windowId);
            kw->setNoBorder(false);
            qCDebug(lcEffect) << "Autotile: restored title bar for" << windowId << "(was on screen:" << screenId << ")";
            m_effect->removeWindowBorder(windowId);
        }
    }
}

bool AutotileHandler::isAutotileScreen(const QString& screenId) const
{
    return m_autotileScreens.contains(screenId);
}

void AutotileHandler::savePreAutotileForDesktopMove(const QString& windowId, const QString& screenId)
{
    // Preserve the window's pre-autotile geometry before onWindowClosed clears it.
    // When the window is re-added on the target desktop, this geometry is restored
    // so that float-restore returns to the original position, not the tiled frame.
    //
    // Stamped with the source screen so the restore path can detect a
    // cross-screen desktop move and decline to apply a saved rect that
    // belongs to a different monitor's coordinate space.
    if (m_preAutotileGeometries.contains(screenId)) {
        const auto& screenGeometries = m_preAutotileGeometries[screenId];
        const QString savedKey = AutotileStateHelpers::findSavedGeometryKey(screenGeometries, windowId);
        if (!savedKey.isEmpty()) {
            m_savedPreAutotileForDesktopMove[windowId] = {screenId, screenGeometries.value(savedKey)};
            qCDebug(lcEffect) << "Preserved pre-autotile geometry for desktop move:" << windowId << "on" << screenId
                              << "rect=" << m_savedPreAutotileForDesktopMove[windowId].second;
        }
    }
}

void AutotileHandler::applyFloatCleanup(const QString& windowId)
{
    m_effect->m_navigationHandler->setWindowFloating(windowId, true);
    // A floating window is no longer tile-managed on any screen — remove
    // its tracking from every screen bucket. If it was borderless anywhere,
    // strip the compositor-side noBorder too by calling setWindowBorderless
    // for each screen that held it.
    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    QStringList screensOwningBorderless;
    for (auto it = m_border.borderlessWindowsByScreen.constBegin(); it != m_border.borderlessWindowsByScreen.constEnd();
         ++it) {
        if (it.value().contains(windowId)) {
            screensOwningBorderless.append(it.key());
        }
    }
    for (const QString& screenId : std::as_const(screensOwningBorderless)) {
        if (w) {
            setWindowBorderless(w, windowId, false, screenId);
        } else {
            AutotileStateHelpers::removeBorderlessOnScreen(m_border, screenId, windowId);
        }
    }
    // Clear tiled tracking on every screen regardless (tiled is a superset of
    // borderless, but a window can be tiled without borderless if hideTitleBars
    // was off when it was tiled).
    AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
    // Drop centering/target tracking too — a floated window isn't being
    // tiled anymore so a stale entry here would trigger centering on the
    // next frameGeometryChanged, snapping the floated window back into an
    // old zone rect. slotWindowsTileRequested no longer clears these
    // globally (it can't without wiping sibling-VS state), so the float
    // path has to clean up after itself.
    m_autotileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    m_effect->removeWindowBorder(windowId);
    unmaximizeMonocleWindow(windowId);
}

} // namespace PlasmaZones
