// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snaphandler.h"

#include "autotilehandler.h"
#include "plasmazoneseffect.h"

#include <effect/effectwindow.h>
#include <window.h>

namespace PlasmaZones {

namespace {
// Drop the server-side decoration while keeping the window filling its zone.
// KWin holds the CLIENT size constant across a decoration change, so calling
// setNoBorder(true) AFTER the zone geometry was applied shrinks the frame by the
// title-bar height and leaves a gap at the bottom of the zone. Re-assert the
// zone rect after dropping the decoration so the content grows to fill it.
//
// CRITICAL: use moveResizeGeometry(), NOT frameGeometry(). On Wayland the latter
// lags behind moveResize() until the client acks the configure, so right after
// applySnapGeometry's moveResize it still reports the window's PRE-snap frame —
// capturing and re-applying that would clobber the snap (the window reverts to
// its floating size/position; this broke snap restore entirely). moveResizeGeometry()
// is the zone rect KWin is already moving toward, set synchronously by moveResize.
// The setNoBorder→moveResize ordering mirrors autotile hiding the title bar
// BEFORE its moveResize (autotilehandler/state.cpp::setWindowBorderless).
void hideTitleBarFillingZone(KWin::Window* kw)
{
    const KWin::RectF zoneTarget = kw->moveResizeGeometry();
    kw->setNoBorder(true);
    // Only re-assert a real target. A degenerate move-resize geometry would
    // otherwise resize the window to nothing; in that case leave the decoration
    // change to settle on its own.
    if (zoneTarget.isValid() && !zoneTarget.isEmpty()) {
        kw->moveResize(zoneTarget);
    }
}
} // namespace

SnapHandler::SnapHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void SnapHandler::markWindowSnapped(const QString& windowId, const QString& screenId)
{
    // An empty screenId is never a valid snap owner: the per-screen buckets are
    // keyed by screenId, so recording under "" would pollute the set with an
    // entry that the per-screen stripOtherScreens cleanup can never reclaim.
    // Callers route unresolved/float windows through clearWindowSnapped instead;
    // this guard is defensive depth for any path that slips an empty screen in.
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    // A window can only be snap-managed by one screen at a time. Strip stale
    // tracking from any OTHER screen — both the tiled and borderless buckets —
    // before recording the new owner (mirrors the autotile cross-screen-transfer
    // cleanup in tiling.cpp).
    const auto stripOtherScreens = [&](QHash<QString, QSet<QString>>& byScreen) {
        for (auto it = byScreen.begin(); it != byScreen.end();) {
            if (it.key() != screenId) {
                it.value().remove(windowId);
            }
            if (it.value().isEmpty() && it.key() != screenId) {
                it = byScreen.erase(it);
            } else {
                ++it;
            }
        }
    };
    stripOtherScreens(m_border.tiledWindowsByScreen);
    stripOtherScreens(m_border.borderlessWindowsByScreen);
    AutotileStateHelpers::addTiledOnScreen(m_border, screenId, windowId);

    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    KWin::Window* kw = w ? w->window() : nullptr;
    // userCanSetNoBorder() — not hasDecoration() — is the correct test for "may
    // this window's server-side title bar be toggled off." It reports whether KWin
    // ALLOWS the no-border toggle, so it stays true for a normal SSD window even
    // while that window is CURRENTLY borderless — exactly the autotile→snap handoff
    // case, where the window arrives already borderless (autotile stripped its
    // decoration) and hasDecoration() therefore reads false. It is false for windows
    // that have no server-side title bar to hide in the first place — client-side-
    // decorated apps (GTK/Electron) and other non-toggleable windows (override-
    // redirect, or decoration forced by a window rule). Using hasDecoration() here
    // skipped the handoff, so snap never recorded the borderless ownership and the
    // deferred restoreWindowBorders un-hid the title bar autotile had hidden.
    // (AutotileHandler::setWindowBorderless still gates on hasDecoration(); the snap
    // side intentionally diverges because only it must survive the already-borderless
    // handoff.)
    if (m_border.hideTitleBars && kw && kw->userCanSetNoBorder()) {
        const bool wasBorderless = AutotileStateHelpers::isBorderlessWindow(m_border, windowId);
        AutotileStateHelpers::addBorderlessOnScreen(m_border, screenId, windowId);
        if (!wasBorderless) {
            hideTitleBarFillingZone(kw);
        }
    }
    // A null w means the window is gone (closed mid-snap); the bucket entry
    // recorded above is then harmless — if the window later closes, slotWindowClosed
    // clears the snap border for it. No border is drawn (nothing to draw on) and none
    // is needed; updateAllBorders() iterates only live windows so it simply skips it.
    //
    // Border overlays are visual-only, so skip the off-desktop case (consistent
    // with updateAllBorders): an OutlinedBorderItem for an invisible window is
    // wasted work. When the user switches to that window's desktop, the
    // desktopChanged → updateAllBorders connection rebuilds its border.
    if (w && w->isOnCurrentDesktop()) {
        m_effect->updateWindowBorder(windowId, w);
    }
}

void SnapHandler::clearWindowSnapped(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    const bool wasBorderless = AutotileStateHelpers::isBorderlessWindow(m_border, windowId);
    AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
    // Restore the title bar only if snap had hidden it AND autotile doesn't
    // still want it borderless. A window mid-transition between modes can be in
    // both sets briefly; un-hiding here would fight autotile's authoritative
    // borderless management and flash the title bar.
    if (wasBorderless && !m_effect->autotileHandler()->isBorderlessWindow(windowId)) {
        if (KWin::EffectWindow* w = m_effect->findWindowById(windowId)) {
            if (KWin::Window* kw = w->window()) {
                kw->setNoBorder(false);
            }
        }
    }
    m_effect->removeWindowBorder(windowId);
}

void SnapHandler::updateSnapHideTitleBars(bool hide)
{
    m_border.hideTitleBars = hide;
    if (hide) {
        // Hide on every currently snap-committed window.
        const auto pairs = AutotileStateHelpers::allTiledPairs(m_border);
        for (const auto& p : pairs) {
            KWin::EffectWindow* w = m_effect->findWindowById(p.first);
            if (!w || !w->hasDecoration()) {
                continue;
            }
            if (!AutotileStateHelpers::isBorderlessWindow(m_border, p.first)) {
                AutotileStateHelpers::addBorderlessOnScreen(m_border, p.second, p.first);
                if (KWin::Window* kw = w->window()) {
                    hideTitleBarFillingZone(kw);
                }
            }
        }
    } else {
        // Restore every window snap had made borderless, except one autotile
        // still wants borderless. A window mid-transition between modes can be in
        // both sets briefly; un-hiding here would fight autotile's authoritative
        // borderless management and flash the title bar (mirrors the guard in
        // clearWindowSnapped / restoreAllSnapBorderless).
        const auto pairs = AutotileStateHelpers::allBorderlessPairs(m_border);
        for (const auto& p : pairs) {
            AutotileStateHelpers::removeBorderlessOnScreen(m_border, p.second, p.first);
            if (m_effect->autotileHandler()->isBorderlessWindow(p.first)) {
                continue;
            }
            if (KWin::EffectWindow* w = m_effect->findWindowById(p.first)) {
                if (KWin::Window* kw = w->window()) {
                    kw->setNoBorder(false);
                }
            }
        }
    }
    m_effect->updateAllBorders();
}

void SnapHandler::restoreAllSnapBorderless()
{
    // Symmetric with AutotileHandler::restoreAllBorderless: on daemon loss or
    // effect teardown the authoritative snap state is gone, so restore every
    // title bar snapping hid and drop the whole snap border set. Without this,
    // snap-hidden windows would keep their title bars hidden until a new snap
    // event or app restart. The isBorderlessWindow guard below is the live
    // protection in the per-window path (clearWindowSnapped); at the teardown
    // call sites AutotileHandler::restoreAllBorderless() runs first and clears
    // its set, so the guard is a belt-and-braces no-op here (a window autotile
    // shares is already un-hidden by then, and a second setNoBorder(false) is
    // harmless/idempotent).
    // This drops the snap border STATE only; callers pair it with
    // clearAllBorders() to tear down the OutlinedBorderItem scene items.
    const auto pairs = AutotileStateHelpers::allBorderlessPairs(m_border);
    for (const auto& p : pairs) {
        if (m_effect->autotileHandler()->isBorderlessWindow(p.first)) {
            continue;
        }
        if (KWin::EffectWindow* w = m_effect->findWindowById(p.first)) {
            if (KWin::Window* kw = w->window()) {
                kw->setNoBorder(false);
            }
        }
    }
    m_border.borderlessWindowsByScreen.clear();
    m_border.tiledWindowsByScreen.clear();
}

void SnapHandler::onWindowClosed(const QString& windowId)
{
    // Pure bookkeeping — the window is being destroyed, so no setNoBorder /
    // removeWindowBorder is needed (the border item is removed by the effect's
    // close path and the title bar dies with the window).
    AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
}

} // namespace PlasmaZones
