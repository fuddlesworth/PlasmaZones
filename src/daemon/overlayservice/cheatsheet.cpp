// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Shortcut cheatsheet overlay — a display-only reference card listing the
// user's global shortcuts, grouped by category and filtered by the tiling
// mode of the screen it opens on. Structural twin of the layout picker
// (singleton passive-shell slot, animator-driven show/hide, dedicated
// Escape ad-hoc grab wired daemon-side); diverges only in being
// non-interactive and in taking its content pushed in by the daemon
// (catalog + mode) rather than resolving it here.

#include "internal.h"
#include "../overlayservice.h"
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

void OverlayService::showCheatsheet(const QString& screenId, const QVariantList& model, const QString& currentMode,
                                    bool autotileAvailable)
{
    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        qCWarning(lcOverlay) << "showCheatsheet: no screen available";
        return;
    }

    const QString resolvedId = screenId.isEmpty() ? PhosphorScreens::ScreenIdentity::identifierFor(screen) : screenId;

    // Same-screen re-request while visible is a no-op (the daemon's toggle
    // handler flips to hideCheatsheet before calling us, so this only
    // triggers for redundant programmatic calls); a DIFFERENT screen
    // migrates the sheet there, mirroring the picker's cross-screen
    // singleton handling.
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

    // Singleton across screens: with the new target validated, dismiss on
    // the previous screen before showing here. Animator-driven hideSlot
    // keys only the cheatsheet track, so sibling slots on the previous
    // shell keep animating cleanly.
    if (m_cheatsheetVisible && !m_cheatsheetScreenId.isEmpty() && m_cheatsheetScreenId != resolvedId) {
        const QString prevScreenId = m_cheatsheetScreenId;
        auto prevIt = m_screenStates.find(prevScreenId);
        if (prevIt != m_screenStates.end() && prevIt->shell && prevIt->shell->shellSurface()
            && prevIt->cheatsheetSlot()) {
            m_shellHost->hideSlot(prevScreenId, PhosphorSlotKeys::Cheatsheet(), [this, prevScreenId]() {
                onCheatsheetSlotHideCompleted(prevScreenId);
            });
        }
    }

    auto* slot = state->cheatsheetSlot();
    auto* shellSurface = state->shell->shellSurface();
    auto* shellWindow = state->shell->shellWindow();

    writeQmlProperty(slot, QStringLiteral("shortcuts"), model);
    writeQmlProperty(slot, QStringLiteral("currentMode"), currentMode);
    writeQmlProperty(slot, QStringLiteral("autotileAvailable"), autotileAvailable);
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
    // Modal — needs input for the backdrop click-to-dismiss.
    syncPassiveShellSurfaceStateForSurface(shellSurface);

    m_cheatsheetScreenId = resolvedId;
    m_cheatsheetVisible = true;

    qCInfo(lcOverlay) << "showCheatsheet: screen=" << resolvedId << "rows=" << model.size() << "mode=" << currentMode;
}

void OverlayService::hideCheatsheet()
{
    if (!m_cheatsheetVisible) {
        // Always emit dismissed so the daemon's Escape-grab release path
        // runs even on idempotent calls — multiple call sites converge
        // here (toggle re-press, backdrop, Escape).
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

void OverlayService::refreshCheatsheet(const QVariantList& model, const QString& currentMode, bool autotileAvailable)
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
    writeQmlProperty(slot, QStringLiteral("shortcuts"), model);
    writeQmlProperty(slot, QStringLiteral("currentMode"), currentMode);
    writeQmlProperty(slot, QStringLiteral("autotileAvailable"), autotileAvailable);
}

void OverlayService::onCheatsheetSlotHideCompleted(const QString& effectiveId)
{
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end() || !it->cheatsheetSlot()) {
        return;
    }
    it->cheatsheetSlot()->setVisible(false);
    writeQmlProperty(it->cheatsheetSlot(), QStringLiteral("loaded"), false);
    syncPassiveShellSurfaceState(effectiveId);
}

void OverlayService::onCheatsheetDismissRequested()
{
    // Backdrop click forwarded from the shell — same route as Escape.
    hideCheatsheet();
}

} // namespace PlasmaZones
