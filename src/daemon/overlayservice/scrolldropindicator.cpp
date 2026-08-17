// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Scroll drag drop-indicator — the per-screen outline of the slot a dragged
// window would land in while a scrolling drag re-insert is armed.
//
// Why this exists at all, when autotile shows drop feedback without any
// overlay: autotile's feedback IS its live restructure — it moves the window
// in the tiling state on every tick and retiles, so neighbours visibly open a
// gap. The scroll engine deliberately cannot do that. It detaches the window
// once at drag start and applies structure at drop (DETACH-ONCE), because
// restructuring live slid the strip out from under a stationary cursor. That
// leaves the drop target invisible, so it has to be painted.
//
// Display-only: unlike the tab strips next door this installs NO input region.
// It is drawn underneath a cursor that is mid-drag, and taking input there
// would break the very drag it describes.

#include "internal.h"
// Full path, because this directory has its own "internal.h" — this is the
// WindowTracking one, for the WindowColorKeys spellings shared with the daemon
// side that produces these overrides.
#include "dbus/windowtrackingadaptor/internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "phosphor_slot_keys.h"
#include "phosphor_roles.h"

#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorScreens/Manager.h>

#include <QColor>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>

namespace PlasmaZones {

void OverlayService::setScrollDropIndicatorOverrides(const QString& screenId, const QVariantMap& overrides)
{
    if (screenId.isEmpty()) {
        return;
    }
    // Change-gated like the tab-strip twin: this runs on every rule/context
    // re-resolve and the common answer is the map it already holds.
    if (m_scrollDropIndicatorOverrides.value(screenId) == overrides) {
        return;
    }
    if (overrides.isEmpty()) {
        m_scrollDropIndicatorOverrides.remove(screenId);
    } else {
        m_scrollDropIndicatorOverrides.insert(screenId, overrides);
    }
    // Deliberately NO replay. The tab strip replays because it is on screen
    // continuously and a paint-rule change must repaint it; the drop indicator
    // exists only during a drag, and every rect push within that drag re-reads
    // these. A change landing between drags shows up on the next drag, which
    // is the first moment anyone could see it.
}

void OverlayService::setScrollDropIndicatorWindowOverrides(const QVariantMap& overrides)
{
    m_scrollDropIndicatorWindowOverrides = overrides;
}

void OverlayService::updateScrollDropIndicator(const QString& screenId, const QRect& rect, bool animate)
{
    if (screenId.isEmpty()) {
        return;
    }

    // The enable toggle folds into the same "no indicator" answer as an empty
    // rect, so one branch below serves both. No cached-rect replay like the
    // tab strips: this rect only exists for the duration of a drag, and
    // toggling the setting mid-drag to see it appear is not a real workflow.
    // The enable gate layers too, and only over the CONTEXT map: the window
    // family is colours only, so there is no per-drag enable to consult.
    // Deliberate — a rule that silences the indicator is a property of where
    // you are working, not of the window you happened to pick up.
    bool indicatorEnabled = !m_settings || m_settings->scrollingDropIndicatorEnabled();
    // constFind on a value() lookup (bound as a lifetime-extended const
    // reference): indexing the member with operator[] default-inserts an
    // empty map for every screen that ever pushed a rect, which then reads
    // back as "has overrides" in the change-gate above. The settings-layering
    // block further down reuses this same lookup for the same reason.
    const QVariantMap& screenOverrides = m_scrollDropIndicatorOverrides.value(screenId);
    if (const auto it = screenOverrides.constFind(WindowPaintKeys::indicatorEnabled());
        it != screenOverrides.constEnd()) {
        indicatorEnabled = it.value().toBool();
    }
    const bool wantsIndicator = indicatorEnabled && rect.isValid() && !rect.isEmpty();

    const auto cachedIt = m_lastScrollDropIndicatorRect.constFind(screenId);
    const bool hadIndicator = cachedIt != m_lastScrollDropIndicatorRect.constEnd();
    const QRect cachedLocal = hadIndicator ? cachedIt.value() : QRect();

    if (!wantsIndicator) {
        if (!hadIndicator) {
            return; // already clear — do not manufacture a shell to hide nothing
        }
        // Dropping the cache here is safe ahead of the bails below: they all
        // mean "nothing is painted on this screen", which is exactly what an
        // absent entry records.
        m_lastScrollDropIndicatorRect.remove(screenId);
        // Hide-and-unload without creating a shell for a screen that never
        // showed an indicator.
        auto it = m_screenStates.find(screenId);
        if (it == m_screenStates.end()) {
            return;
        }
        QQuickItem* existingSlot = it->scrollDropIndicatorSlot();
        if (!existingSlot || !existingSlot->isVisible()) {
            return;
        }
        // A hide is already animating and its completion owns the teardown.
        // Re-entering hideSlot would bump the guard, making the in-flight
        // completion stale-return, and leave the teardown resting on the
        // animator emitting a fresh completion for the re-entered hide.
        if (m_scrollDropIndicatorHidePending.contains(screenId)) {
            return;
        }
        // Generation-guard the animated hide, same contract as the tab strips:
        // with animations DISABLED, SurfaceAnimator salvages a superseded
        // onComplete and fires it synchronously from inside a later beginShow,
        // i.e. after that path has already written loaded=true and
        // setVisible(true), which this callback would otherwise clobber.
        const quint64 hideGeneration = ++m_scrollDropIndicatorHideGuard[screenId];
        m_scrollDropIndicatorHidePending.insert(screenId);
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::ScrollDropIndicator(), [this, screenId, hideGeneration]() {
            if (m_scrollDropIndicatorHideGuard.value(screenId) != hideGeneration) {
                return; // superseded by a newer rect
            }
            m_scrollDropIndicatorHidePending.remove(screenId);
            auto stateIt = m_screenStates.find(screenId);
            if (stateIt == m_screenStates.end()) {
                return;
            }
            QQuickItem* slot = stateIt->scrollDropIndicatorSlot();
            if (!slot) {
                return;
            }
            slot->setVisible(false);
            writeQmlProperty(slot, QStringLiteral("loaded"), false);
            syncPassiveShellSurfaceState(screenId);
        });
        return;
    }

    QScreen* screen = resolveTargetScreen(m_screenManager, screenId);
    if (!screen) {
        return;
    }
    QRect screenGeom = resolveScreenGeometry(m_screenManager, screenId);
    if (!screenGeom.isValid()) {
        screenGeom = screen->geometry();
    }

    // The rect arrives in absolute compositor coordinates; the shell window
    // sits at the screen origin, so shift into window space here (single
    // conversion point, matching the tab strips).
    const QRect local = rect.translated(-screenGeom.x(), -screenGeom.y());

    // Change-gate on the SHELL-LOCAL rect, which is what actually gets painted.
    // A drag pushes on every pointer tick and the target only changes when the
    // cursor crosses into a different column or stack slot, so the common call
    // is a repeat; without this gate every tick would re-write the QML
    // properties and re-run a surface sync for no visual change.
    //
    // Local rather than absolute deliberately: if the screen origin moves under
    // a held target, the absolute rect is UNCHANGED while the window-local one
    // is not, so an absolute key would early-return and leave the indicator
    // painting at its pre-move position. Gating here also means a repeat tick
    // never reaches ensurePassiveShellFor below, which re-asserts the SHELL's
    // click-through flag as a side effect.
    if (hadIndicator && cachedLocal == local) {
        return;
    }

    auto* state = ensurePassiveShellFor(screenId, screen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->scrollDropIndicatorSlot()) {
        qCWarning(lcOverlay) << "updateScrollDropIndicator: no passive shell for screen=" << screenId;
        return;
    }

    // Commit the cache only once past every bail above. Recording it earlier
    // was a silent trap: after a bail the cache claimed a rect was painted when
    // nothing was, so every later identical push compared equal, early-returned
    // at the gate, and the indicator never appeared again for that drag.
    m_lastScrollDropIndicatorRect.insert(screenId, local);

    // Invalidate a pending hide only once this update is committed to running
    // the show choreography below. Bumping before the bails above would leave
    // the in-flight hide's completion stale-returning while nothing re-showed
    // the slot, stranding it visible+loaded at opacity 0 forever.
    ++m_scrollDropIndicatorHideGuard[screenId];
    const bool hideWasInFlight = m_scrollDropIndicatorHidePending.remove(screenId);

    auto* slot = state->scrollDropIndicatorSlot();
    auto* shellSurface = state->shell->shellSurface();
    auto* shellWindow = state->shell->shellWindow();

    // BEFORE the rect, so the Behaviors are already gated when the new value
    // lands. Written after it, the change would animate under the old flag and
    // the first frame would still stretch. The first rect of a (re)show
    // snaps rather than tweening in from the previous drag's stale rect —
    // hadIndicator is exactly "the slot is already showing a rect".
    writeQmlProperty(slot, QStringLiteral("animateMoves"), animate && hadIndicator);
    writeQmlProperty(slot, QStringLiteral("indicatorRect"), local);

    // Paint settings, pushed on every RECT CHANGE rather than only on the
    // show path (the gate above returns first when the rect is unchanged, so
    // a mid-drag settings edit lands on the next target change rather than
    // immediately — deliberate, per the no-replay note at the top). A drag is short enough that a settings change
    // almost never lands mid-drag, but writing an unchanged QML property emits no change notification, so the cost of
    // being correct here is five compares. Every colour that reaches the content item is CONCRETE: the
    // follow-the-theme sentinel is resolved in Settings, and both override tiers are shape-checked hex.
    if (m_settings) {
        // Three layers, narrowest first: the DRAGGED WINDOW's rule beats the
        // screen's CONTEXT rule, which beats the setting, which Settings has
        // already resolved against the palette when it holds the empty
        // sentinel. That is the tab colours' order, and niri's.
        const QVariantMap& ctx = screenOverrides;
        const QVariantMap& win = m_scrollDropIndicatorWindowOverrides;
        const auto layered = [&ctx, &win](const QString& key, const QVariant& fromSettings) {
            if (const auto it = win.constFind(key); it != win.constEnd()) {
                return it.value();
            }
            if (const auto it = ctx.constFind(key); it != ctx.constEnd()) {
                return it.value();
            }
            return fromSettings;
        };
        // The two colour slots read their spelling from WindowColorKeys, which
        // is both the override-map key the daemon writes and the QML property
        // name this slot exposes — the same string in both argument positions,
        // which is exactly the pair a typo used to break silently. The three
        // non-colour properties below carry no shared key and stay literal.
        //
        // The settings values arrive RESOLVED (Settings owns the theme
        // fallback), so they are spelled back as #AARRGGBB to keep every tier
        // of `layered` the same type — the two override tiers carry hex
        // strings, checked for that shape before they are admitted.
        writeQmlProperty(slot, WindowColorKeys::indicatorColor(),
                         layered(WindowColorKeys::indicatorColor(),
                                 m_settings->scrollingDropIndicatorColor().name(QColor::HexArgb)));
        writeQmlProperty(slot, WindowColorKeys::indicatorBorderColor(),
                         layered(WindowColorKeys::indicatorBorderColor(),
                                 m_settings->scrollingDropIndicatorBorderColor().name(QColor::HexArgb)));
        writeQmlProperty(slot, WindowPaintKeys::indicatorOpacity(),
                         layered(WindowPaintKeys::indicatorOpacity(), m_settings->scrollingDropIndicatorOpacity()));
        writeQmlProperty(
            slot, WindowPaintKeys::indicatorBorderWidth(),
            layered(WindowPaintKeys::indicatorBorderWidth(), m_settings->scrollingDropIndicatorBorderWidth()));
        writeQmlProperty(
            slot, WindowPaintKeys::indicatorBorderRadius(),
            layered(WindowPaintKeys::indicatorBorderRadius(), m_settings->scrollingDropIndicatorBorderRadius()));
    }

    if (slot->isVisible() && !hideWasInFlight) {
        // Live rect update — no show choreography needed, but the sync is NOT
        // optional. ensurePassiveShellFor above unconditionally re-asserts
        // Qt::WindowTransparentForInput on the SHELL window, which is shared
        // with every other slot on this screen; skipping the sync would leave a
        // live tab-strip input region dropped until its next strip update. The
        // earlier "no input to sync" reasoning was about THIS slot's own
        // (absent) region and missed that the flag it clobbers is the shell's.
        syncPassiveShellSurfaceStateForSurface(shellSurface);
        return;
    }
    // Either genuinely hidden, or mid-hide (still visible, opacity animating
    // toward 0). In BOTH cases the show choreography must run: early-returning
    // on a fading slot would leave it visible+loaded at opacity 0 forever once
    // the superseded hide completion no-ops.

    if (shellWindow) {
        assertWindowOnScreen(shellWindow, screen, screenGeom);
        shellWindow->setWidth(screenGeom.width());
        shellWindow->setHeight(screenGeom.height());
    }
    writeQmlProperty(slot, QStringLiteral("loaded"), true);

    cancelSurfacePrime(shellSurface);
    if (!shellSurface->isLogicallyShown()) {
        shellSurface->show();
    }
    slot->setVisible(true);
    m_surfaceAnimator->beginShow(shellSurface, slot, PhosphorRoles::ScrollDropIndicator, []() { });
    // Re-derive the surface's input state after Surface::show() cleared the
    // transparent-input flag. This slot contributes no region of its own, so
    // the sync is what keeps the shell click-through for the rest of the drag.
    syncPassiveShellSurfaceStateForSurface(shellSurface);
}

} // namespace PlasmaZones
