// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Shortcut cheatsheet overlay — a reference card listing the user's global
// shortcuts, grouped by category and filtered by the tiling mode of the screen
// it opens on, with a filter field for looking one up. Structural twin of the
// layout picker (singleton passive-shell slot, animator-driven show/hide,
// dedicated Escape ad-hoc grab wired daemon-side). It diverges in taking its
// content pushed in by the daemon (catalog + mode) rather than resolving it
// here, and in being the one slot that asks the shared passive shell for the
// keyboard, so its search field can receive typed characters.

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "phosphor_slot_keys.h"
#include "phosphor_roles.h"

#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>

namespace PlasmaZones {

namespace {

/// The sheet's whole content surface, in one place. Both entry points push the
/// identical set, and a field added to one and not the other is a divergence
/// the compiler cannot catch: the sheet would simply keep stale content after
/// a live mode switch.
void writeCheatsheetContent(QQuickItem* slot, const QVariantList& model, const QString& currentMode,
                            bool autotileAvailable, bool scrollingAvailable, bool layoutsAvailable,
                            bool layoutsAreTemplates)
{
    writeQmlProperty(slot, QStringLiteral("shortcuts"), model);
    writeQmlProperty(slot, QStringLiteral("currentMode"), currentMode);
    writeQmlProperty(slot, QStringLiteral("autotileAvailable"), autotileAvailable);
    writeQmlProperty(slot, QStringLiteral("scrollingAvailable"), scrollingAvailable);
    writeQmlProperty(slot, QStringLiteral("layoutsAvailable"), layoutsAvailable);
    writeQmlProperty(slot, QStringLiteral("layoutsAreTemplates"), layoutsAreTemplates);
}

} // namespace

void OverlayService::showCheatsheet(const QString& screenId, const QVariantList& model, const QString& currentMode,
                                    bool autotileAvailable, bool scrollingAvailable, bool layoutsAvailable,
                                    bool layoutsAreTemplates)
{
    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showCheatsheet: no screen available";
        return;
    }

    const QString resolvedId = screenId.isEmpty() ? PhosphorScreens::ScreenIdentity::identifierFor(screen) : screenId;

    // Same-screen re-request while visible is a no-op (the daemon's toggle
    // handler flips to hideCheatsheet before calling us, so this only
    // triggers for redundant programmatic calls). A DIFFERENT screen migrates
    // the sheet there, mirroring the picker's cross-screen singleton handling.
    //
    // The migration arm is defensive rather than live: the sheet's only entry
    // point is the toggle, which hides and returns when the sheet is already
    // up, and showCheatsheet is on neither IOverlayService nor D-Bus. So
    // m_cheatsheetVisible is false here today. It is kept correct, rather than
    // dropped, because the singleton invariant is the picker's too and a
    // second caller would arrive without it.
    if (m_cheatsheetVisible && m_cheatsheetScreenId == resolvedId) {
        return;
    }

    if (model.isEmpty()) {
        qCWarning(lcOverlay) << "showCheatsheet: empty shortcut catalog — nothing to display";
        return;
    }

    QRect screenGeom = resolveScreenGeometry(m_screenManager, resolvedId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    auto* state = ensurePassiveShellFor(resolvedId, screen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->cheatsheetSlot()) {
        qCWarning(lcOverlay) << "showCheatsheet: no passive shell for screen=" << resolvedId;
        return;
    }

    // Latch the singleton state here, ahead of both the previous-screen hide
    // and the content pushes, exactly as showLayoutPicker does. Two reasons,
    // and neither is about this function's own reads. The lib's hideSlot runs
    // its completion inline on every benign-no-op branch, so the handler can
    // re-enter while we are still mid-show; and OverlayService is a QML context
    // property, so a binding evaluated during a push can reach back in. Both
    // must observe the sheet as visible-on-the-new-screen, or their hide takes
    // the idempotent branch and the sheet ends up shown with nothing recording
    // it. The keyboard predicate reads these two members too (see
    // shellhost_bridge.cpp) — it has to, because releasing on the first edge of
    // dismissal means it cannot key on a slot that stays visible for the whole
    // fade-out — so they are also set before the sync at the end.
    const QString prevScreenId = m_cheatsheetVisible ? m_cheatsheetScreenId : QString();
    m_cheatsheetScreenId = resolvedId;
    m_cheatsheetVisible = true;

    // Singleton across screens: with the new target validated, dismiss on
    // the previous screen before showing here. Animator-driven hideSlot
    // keys only the cheatsheet track, so sibling slots on the previous
    // shell keep animating cleanly.
    if (!prevScreenId.isEmpty() && prevScreenId != resolvedId) {
        auto prevIt = m_screenStates.find(prevScreenId);
        if (prevIt != m_screenStates.end() && prevIt->shell && prevIt->shell->shellSurface()
            && prevIt->cheatsheetSlot()) {
            m_shellHost->hideSlot(prevScreenId, PhosphorSlotKeys::Cheatsheet(), [this, prevScreenId]() {
                onCheatsheetSlotHideCompleted(prevScreenId);
            });
        }
        // Drop the previous surface's keyboard grab on this edge rather than
        // waiting for its fade to finish. The slot stays visible for the whole
        // hide animation, so deferring to the completion would leave two layer
        // surfaces asking for an exclusive keyboard at once, which is the
        // arrangement the release-on-first-edge rule exists to prevent. Safe
        // to run after the latch: the predicate compares the cheatsheet's
        // screen id against the id being synced, and that is now the new
        // screen, so the previous one correctly computes kbd-None.
        syncPassiveShellSurfaceState(prevScreenId);
    }

    auto* slot = state->cheatsheetSlot();
    auto* shellSurface = state->shell->shellSurface();
    auto* shellWindow = state->shell->shellWindow();

    writeCheatsheetContent(slot, model, currentMode, autotileAvailable, scrollingAvailable, layoutsAvailable,
                           layoutsAreTemplates);
    writeFontProperties(slot, m_settings, /*includeLabelFontColor=*/false);

    // Same SurfaceDecoration host the picker uses, retargeted to the
    // cheatsheet's surface path. Empty resolution = no decoration (card
    // draws natively).
    applyDecoration(slot, QStringLiteral("popup.cheatsheet"));

    if (shellWindow) {
        assertWindowOnScreen(shellWindow, screen, screenGeom);
        shellWindow->setWidth(screenGeom.width());
        shellWindow->setHeight(screenGeom.height());
    }

    // OSD-style content lifecycle: toggle false→true so the Loader
    // re-instantiates CheatsheetContent fresh per show.
    writeQmlProperty(slot, QStringLiteral("loaded"), false);
    writeQmlProperty(slot, QStringLiteral("loaded"), true);

    cancelSurfacePrime(shellSurface);
    if (!shellSurface->isLogicallyShown()) {
        shellSurface->show();
    }
    slot->setVisible(true);
    m_surfaceAnimator->beginShow(shellSurface, slot, PhosphorRoles::Cheatsheet, []() { });

    // Modal — needs input for the backdrop click-to-dismiss, and the keyboard
    // for the search field. The singleton state was latched above, before the
    // pushes, so the keyboard predicate reads the new screen here.
    syncPassiveShellSurfaceStateForSurface(shellSurface);

    qCInfo(lcOverlay) << "showCheatsheet: screen=" << resolvedId << "rows=" << model.size() << "mode=" << currentMode;
}

void OverlayService::hideCheatsheet()
{
    if (!m_cheatsheetVisible) {
        // Always emit dismissed so the daemon's Escape-grab release path
        // runs even on idempotent calls — multiple call sites converge
        // here (toggle re-press, backdrop, Escape). A deliberate exception to
        // the emit-only-when-the-value-changed rule: the signal is carrying a
        // grab release, not a state change, and making it conditional would
        // strand the Escape grab on exactly the paths that converge here.
        // Do not "fix" it to match the rule.
        Q_EMIT cheatsheetDismissed();
        return;
    }

    const QString screenId = m_cheatsheetScreenId;
    m_cheatsheetVisible = false;
    m_cheatsheetScreenId.clear();

    // Dismissed BEFORE hideSlot so listeners see "dismissed first, then
    // completion" regardless of whether the completion runs synchronously
    // or asynchronously — same ordering contract as the picker.
    Q_EMIT cheatsheetDismissed();

    // Hand the keyboard back now, not when the fade finishes. The sheet is the
    // one overlay that takes keyboard focus for its search field (see the
    // anyKeyboardGrabbing derivation in shellhost_bridge.cpp), and the slot
    // stays visible for the whole hide animation — releasing on completion
    // would eat every keystroke aimed at the window the user just came back
    // to. m_cheatsheetVisible is already false above, so this recomputes to
    // kbd-None while leaving the mapped/input-region state untouched.
    syncPassiveShellSurfaceState(screenId);

    auto stateIt = m_screenStates.find(screenId);
    if (stateIt != m_screenStates.end() && stateIt->shell && stateIt->shell->shellSurface()
        && stateIt->cheatsheetSlot()) {
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::Cheatsheet(), [this, effectiveId = screenId]() {
            onCheatsheetSlotHideCompleted(effectiveId);
        });
    }
}

bool OverlayService::isCheatsheetVisible() const
{
    return m_cheatsheetVisible;
}

QString OverlayService::cheatsheetScreenId() const
{
    return m_cheatsheetScreenId;
}

void OverlayService::refreshCheatsheet(const QVariantList& model, const QString& currentMode, bool autotileAvailable,
                                       bool scrollingAvailable, bool layoutsAvailable, bool layoutsAreTemplates)
{
    if (!m_cheatsheetVisible) {
        return;
    }
    // Same contract as showCheatsheet's empty-catalog refusal: never blank a
    // visible sheet with an empty push; keep the last good content instead.
    if (model.isEmpty()) {
        return;
    }
    auto it = m_screenStates.find(m_cheatsheetScreenId);
    if (it == m_screenStates.end() || !it->cheatsheetSlot()) {
        return;
    }
    auto* slot = it->cheatsheetSlot();
    writeCheatsheetContent(slot, model, currentMode, autotileAvailable, scrollingAvailable, layoutsAvailable,
                           layoutsAreTemplates);
}

void OverlayService::onCheatsheetSlotHideCompleted(const QString& effectiveId)
{
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end() || !it->cheatsheetSlot()) {
        return;
    }
    it->cheatsheetSlot()->setVisible(false);
    writeQmlProperty(it->cheatsheetSlot(), QStringLiteral("loaded"), false);
    // Release the backdrop stand-in, matching onOsdSlotHideCompleted: a hidden
    // slot draws none of it, the image is wallpaper-sized, and every show runs
    // applyDecoration again, which rewrites it.
    writeQmlProperty(it->cheatsheetSlot(), QStringLiteral("backdropTexture"), QVariant());
    syncPassiveShellSurfaceState(effectiveId);
}

void OverlayService::onCheatsheetDismissRequested()
{
    // Backdrop click forwarded from the shell — same route as Escape.
    hideCheatsheet();
}

} // namespace PlasmaZones
