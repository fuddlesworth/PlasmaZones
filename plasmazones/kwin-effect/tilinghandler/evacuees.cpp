// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tiling handler's half of an output-change settle: a window KWin moved
// off an output that went away (an evacuee) and a window KWin returned to it.
// The daemon has already placed each one by the time the effect gets its
// verdict, so these only bring the effect's own tracking in line, with no
// announce and no claim handed back to KWin. Part of TilingHandler.

#include "tilinghandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <effect/effectwindow.h>
#include <window.h>

namespace PlasmaZones {

bool TilingHandler::trackWithoutAnnounce(KWin::EffectWindow* w, const QString& screenId, bool knownFreeFloating)
{
    const QString windowId = m_effect->getWindowId(w);
    // Consume the spawn-provenance marker UNCONDITIONALLY — a short-circuit
    // (|| with remove second) would leave the entry behind whenever the
    // caller already passed true, and a later RE-ADD that deliberately
    // passes false would then flip to true off the stale entry.
    const bool wasFresh = m_pendingFreshWindows.remove(windowId) > 0;
    m_notifiedWindows.insert(windowId);
    m_notifiedWindowScreens[windowId] = screenId;
    // Save pre-autotile geometry BEFORE the daemon tiles the window.
    // Without this, a window launched directly into autotile has no saved
    // geometry, and floating it would leave it at its tiled position.
    // knownFreeFloating is passed EXPLICITLY by every caller (see the
    // header): true lets the genuine open bypass the floating guard, false
    // makes a re-add run it against a rect that is not free geometry.
    saveAndRecordPreTileGeometry(windowId, screenId, w, w->frameGeometry(), knownFreeFloating || wasFresh);
    return wasFresh;
}

void TilingHandler::scrubClaimsForEvacuation(const QString& windowId, KWin::EffectWindow* w)
{
    // Windowed fullscreen: the membership goes, and with it PlasmaZones'
    // keep-below demotion. KWin re-fullscreens an evacuee on the output it
    // lands on, and a keep-below fullscreen window would sit under every
    // window there. The layer flags are not part of the state KWin compares
    // when it returns a window, so restoring them does not count as a touch
    // (F710).
    m_windowedFsClearInFlight.remove(windowId);
    forgetWindowedFullscreen(windowId);
    restoreWindowedFullscreenLayerDemotion(windowId, (w && !w->isDeleted()) ? w->window() : nullptr);
    // Maximize-to-edges: the entry goes and the maximize stays. Monocle needs
    // nothing here: cleanupClosedWindowState in the funnel already scrubbed it
    // bare.
    m_maximizedToEdgesWindows.remove(windowId);
}

void TilingHandler::adoptEvacuee(KWin::EffectWindow* w, const QString& screenId)
{
    if (!w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    // No release relay for the output it left: the daemon parked and pruned
    // that output, and a release now would retile its dead tiles over the
    // other evacuees (F617). A monocle claim is read first, since the funnel
    // scrubs it bare: KWin keeps the maximize, and the first monocle batch after
    // the window's return takes the claim back (F685, evacuee_keeps_kwin_state).
    const bool heldMonocle = m_monocleMaximizedWindows.contains(windowId);
    // A minimize-float the same way: the funnel drops it, and it stays dropped
    // while the window is away, so one unminimized where KWin put it floats
    // there (F643, evacuee_floats). Its return gives it back (F1046).
    const bool heldMinimizeFloat = isMinimizeFloated(windowId);
    const bool untiledMinimizeFloat = m_minimizeFloatMarks.isUntiled(windowId);
    cleanupAutotileTracking(windowId, ScrollDecisions::ClaimScope::Evacuation);
    if (heldMonocle) {
        m_evacueeMonocleClaims.insert(windowId);
    }
    if (heldMinimizeFloat) {
        m_evacueeMinimizeFloats.insert(windowId, untiledMinimizeFloat);
    }
    // The daemon floated it in place on a tiling destination. Tracked here so
    // the destination's engine owns it, and never announced: an announce
    // would insert it as a tile and reflow the windows already there (F642).
    if (m_managedScreens.contains(screenId)) {
        trackWithoutAnnounce(w, screenId, /*knownFreeFloating=*/false);
    }
}

void TilingHandler::readoptEvacuee(KWin::EffectWindow* w, const QString& screenId)
{
    if (!w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    // The float hold it had where KWin put it while its output was away. The
    // daemon released it; tracking on the same output may already name the
    // returned place (its re-seat batch pre-seeds the screen), and is kept.
    if (m_notifiedWindows.contains(windowId)
        && !PhosphorIdentity::VirtualScreenId::samePhysical(m_notifiedWindowScreens.value(windowId), screenId)) {
        cleanupAutotileTracking(windowId, ScrollDecisions::ClaimScope::Evacuation);
    }
    if (m_notifiedWindows.contains(windowId)) {
        m_notifiedWindowScreens[windowId] = screenId;
    } else if (m_managedScreens.contains(screenId)) {
        trackWithoutAnnounce(w, screenId, /*knownFreeFloating=*/false);
    }
    // The minimize-float it held when its output went away (adoptEvacuee).
    // The daemon re-seated it floating, so the effect owns that float again,
    // as the cross-screen hop re-establishes one (handleWindowOutputChanged).
    // With no peer record from before the evacuation, its unminimize commits
    // at once. One unminimized while away has passed that edge, and is
    // unfloated into its tile now (F1046).
    const auto parked = m_evacueeMinimizeFloats.constFind(windowId);
    if (parked == m_evacueeMinimizeFloats.constEnd()) {
        return;
    }
    const bool untiled = parked.value();
    m_evacueeMinimizeFloats.erase(parked);
    if (!m_notifiedWindows.contains(windowId)) {
        return;
    }
    m_minimizeFloatedWindows.insert(windowId);
    if (untiled) {
        m_minimizeFloatMarks.markUntiled(windowId);
    }
    m_minimizeFloatMarks.markDisplaced(windowId);
    if (!w->isMinimized()) {
        dispatchUnminimizeUnfloat(windowId, screenId);
    }
}

QStringList TilingHandler::stripMembersOn(const QString& screenId) const
{
    QStringList members;
    for (const QString& scrollScreen : std::as_const(m_scrollingScreens)) {
        if (!PhosphorIdentity::VirtualScreenId::samePhysical(scrollScreen, screenId)) {
            continue;
        }
        const auto bucket = m_border.tiledWindowsByScreen.constFind(scrollScreen);
        if (bucket != m_border.tiledWindowsByScreen.constEnd()) {
            for (const QString& windowId : bucket.value()) {
                members.append(windowId);
            }
        }
    }
    return members;
}

} // namespace PlasmaZones
