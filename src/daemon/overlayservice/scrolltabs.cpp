// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Scroll tab indicators — per-screen markers for the tabs of tabbed scrolling
// columns. Unlike the cheatsheet/picker this slot is NOT a daemon-level
// singleton (every scrolling screen can carry strips concurrently), and it
// updates by plain property writes because the model refreshes on every strip
// relayout.
//
// It takes input ONLY where it draws: this file builds a per-screen QRegion of
// the indicator rects and hands it to the shell, so a click on a tab activates
// that window while a click anywhere else falls through to the window beneath.
// That is a middle state the shell gained for this slot — every other kbd-None
// slot is still all-or-nothing.

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "phosphor_slot_keys.h"
#include "phosphor_roles.h"

#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorScreens/Manager.h>

#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>

namespace PlasmaZones {

void OverlayService::replayScrollTabStrips()
{
    // Load-bearing copy: updateScrollTabStrips writes m_lastScrollTabStrips
    // (insert on a live model, remove on an empty one), so iterating the
    // member directly would walk a container mutating underneath us.
    const QHash<QString, QVariantList> cached = m_lastScrollTabStrips;
    for (auto it = cached.constBegin(); it != cached.constEnd(); ++it) {
        updateScrollTabStrips(it.key(), it.value());
    }
}

void OverlayService::onScrollTabActivated(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    Q_EMIT scrollTabActivated(windowId);
}

void OverlayService::setScrollTabIndicatorOverrides(const QString& screenId, const QVariantMap& overrides)
{
    if (screenId.isEmpty()) {
        return;
    }
    // Change-gated: this runs on every rule/context re-resolve, and the common
    // answer is the same map it already held. Replaying unconditionally would
    // re-push the whole strip model on every desktop switch.
    if (m_scrollTabIndicatorOverrides.value(screenId) == overrides) {
        return;
    }
    if (overrides.isEmpty()) {
        m_scrollTabIndicatorOverrides.remove(screenId);
    } else {
        m_scrollTabIndicatorOverrides.insert(screenId, overrides);
    }
    // Replay THIS screen only. The engine's tabStripsChanged is change-latched
    // on structure, so nothing else repaints after a pure paint-rule change.
    const auto cached = m_lastScrollTabStrips.constFind(screenId);
    if (cached != m_lastScrollTabStrips.constEnd()) {
        updateScrollTabStrips(screenId, cached.value());
    }
}

void OverlayService::updateScrollTabStrips(const QString& screenId, const QVariantList& strips)
{
    if (screenId.isEmpty()) {
        return;
    }

    // Cache the live model regardless of the enable toggle, so flipping the
    // indicator back on can replay the current strips immediately — the
    // engine's tabStripsChanged is change-latched and would otherwise stay
    // silent until the next structural strip change. The settings hook
    // (overlayservice/settings.cpp) replays this cache through THIS function
    // on every toggle, and the disabled branch below turns a replay into a
    // hide, so one entry point serves both directions.
    if (strips.isEmpty()) {
        m_lastScrollTabStrips.remove(screenId);
    } else {
        m_lastScrollTabStrips.insert(screenId, strips);
    }
    const bool stripEnabled = !m_settings || m_settings->scrollingTabIndicatorEnabled();

    if (strips.isEmpty() || !stripEnabled) {
        // Drop the input region on BOTH no-indicator paths, not just the
        // empty-strips one. Switching the indicator off with strips still live
        // takes this branch too, and without the remove the entry would linger
        // for the whole disabled period, so any unrelated surface-state sync
        // would re-install a region for an indicator that is not there.
        //
        // This mostly does not shorten the click-through window during the
        // animated hide: nothing is pushed to the shell until a sync runs, and
        // on this branch that is the hide-completion callback below, so the
        // fading pills normally stay clickable for the length of the
        // animation. The exception is an UNRELATED sync landing mid-animation
        // (an OSD, the zone selector, a geometry change), which now reads the
        // empty region and drops the pills' input early. That is the safe
        // direction to fail in.
        m_scrollTabInputRegions.remove(screenId);
        // Hide-and-unload without creating a shell for a screen that never
        // showed strips.
        auto it = m_screenStates.find(screenId);
        if (it == m_screenStates.end()) {
            return;
        }
        QQuickItem* existingSlot = it->scrollTabsSlot();
        if (!existingSlot || !existingSlot->isVisible()) {
            return;
        }
        // A hide is already animating for this screen and its completion owns
        // the teardown. Re-entering hideSlot would bump the guard, making the
        // in-flight completion stale-return, and leave the teardown resting on
        // the animator emitting a fresh completion for the re-entered hide.
        if (m_scrollTabsHidePending.contains(screenId)) {
            return;
        }
        // Generation-guard the animated hide. With animations ENABLED a
        // superseding beginShow pre-cancels this track, and a cancel is not a
        // completion, so the callback below never runs at all. The path the
        // guard actually protects is SurfaceAnimator's animations-DISABLED
        // branch, which salvages a superseded onComplete and fires it
        // SYNCHRONOUSLY from inside the beginShow call on the show path — i.e.
        // after that path has already written loaded=true and setVisible(true),
        // which this callback would otherwise clobber. The show path pairs with
        // the guard by treating hide-in-flight as "not visible" so it re-runs
        // beginShow instead of early-returning on a slot that is fading out.
        const quint64 hideGeneration = ++m_scrollTabsHideGuard[screenId];
        m_scrollTabsHidePending.insert(screenId);
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::ScrollTabs(), [this, screenId, hideGeneration]() {
            if (m_scrollTabsHideGuard.value(screenId) != hideGeneration) {
                return; // superseded by a newer non-empty update
            }
            m_scrollTabsHidePending.remove(screenId);
            auto stateIt = m_screenStates.find(screenId);
            if (stateIt == m_screenStates.end()) {
                return;
            }
            QQuickItem* slot = stateIt->scrollTabsSlot();
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

    auto* state = ensurePassiveShellFor(screenId, screen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->scrollTabsSlot()) {
        qCWarning(lcOverlay) << "updateScrollTabStrips: no passive shell for screen=" << screenId;
        return;
    }

    // Invalidate a pending hide only once this update is committed to running
    // the show choreography below. Bumping the guard BEFORE the two bails above
    // was unrecoverable: the in-flight hide's completion would stale-return
    // while nothing re-showed the slot, so the animator drove opacity to 0 and
    // left the slot visible+loaded at opacity 0 permanently — and because
    // anyVisible stayed true the wl_surface never unmapped, while every later
    // non-empty update hit the still-visible early return below. Nothing
    // between the bump's old position and here mutates slot state, so deferring
    // it is safe.
    ++m_scrollTabsHideGuard[screenId];
    const bool hideWasInFlight = m_scrollTabsHidePending.remove(screenId);

    // The strip rects arrive in absolute compositor coordinates; the shell
    // window is positioned at the screen origin, so shift into window
    // space here (single conversion point).
    QVariantList shifted;
    shifted.reserve(strips.size());
    QRegion inputRegion;
    for (const QVariant& entry : strips) {
        QVariantMap strip = entry.toMap();
        const int x = strip.value(QStringLiteral("x")).toInt() - screenGeom.x();
        const int y = strip.value(QStringLiteral("y")).toInt() - screenGeom.y();
        strip.insert(QStringLiteral("x"), x);
        strip.insert(QStringLiteral("y"), y);
        shifted.append(strip);
        // The input region is the DRAWN rect, not a padded hit box. A padded
        // one would take clicks off the indicator's edge — on a thin bar most
        // of the padding lands on the window beside it, which is a worse
        // trade than a small target. The rects are already window-local here,
        // which is the coordinate space the shell's mask wants.
        inputRegion +=
            QRect(x, y, strip.value(QStringLiteral("width")).toInt(), strip.value(QStringLiteral("height")).toInt());
    }
    m_scrollTabInputRegions.insert(screenId, inputRegion);

    auto* slot = state->scrollTabsSlot();
    auto* shellSurface = state->shell->shellSurface();
    auto* shellWindow = state->shell->shellWindow();

    writeQmlProperty(slot, QStringLiteral("strips"), shifted);

    // View slide, mirrored from the tile batch. The rects above are already at
    // their FINAL positions, so the overlay starts this many pixels behind and
    // springs to zero — the same shape, profile and starting instant as the
    // compositor's per-output spring, which is what keeps an indicator glued
    // to the column it labels while the strip slides.
    //
    // The sequence number is what makes it re-trigger: two identical scrolls in
    // a row push the same delta, and a bare property write would raise no
    // change signal for the second, leaving the indicator behind. QML keys the
    // restart on the sequence rather than the value.
    const int viewDeltaX = shifted.isEmpty() ? 0 : shifted.first().toMap().value(QStringLiteral("viewDeltaX")).toInt();
    if (viewDeltaX != 0) {
        writeQmlProperty(slot, QStringLiteral("viewDeltaX"), viewDeltaX);
        writeQmlProperty(slot, QStringLiteral("viewDeltaSeq"), ++m_scrollTabViewDeltaSeq[screenId]);
    }

    // Paint settings, pushed on EVERY update rather than only on the show path
    // like the font block below. The settings hook replays the cached strips
    // through this function when any of them changes, and that replay lands on
    // an already-visible slot, so a show-path-only push would leave a live
    // indicator painting yesterday's colours until it next hid. Writing an
    // unchanged QML property emits no change notification, so the cost of
    // re-pushing these on a plain relayout is the six compares.
    if (m_settings) {
        // Context rules win per property over the config value, so a rule that
        // sets only the style leaves the colours on their configured values.
        const QVariantMap overrides = m_scrollTabIndicatorOverrides.value(screenId);
        const auto push = [&](const char* property, const QVariant& configured) {
            const QString key = QString::fromLatin1(property);
            writeQmlProperty(slot, key, overrides.value(key, configured));
        };
        // "tabStyle", not "style": these names address the SLOT's declared
        // properties (PassiveOverlayShell's scrollTabsSlot), which forwards
        // them into the content item. The slot is a shared-shape Item, so the
        // tab-specific spelling keeps it unambiguous there; the content item's
        // own property is plain `style`.
        push("tabStyle", m_settings->scrollingTabIndicatorStyle());
        push("gapsBetweenTabs", m_settings->scrollingTabIndicatorGapsBetweenTabs());
        push("cornerRadius", m_settings->scrollingTabIndicatorCornerRadius());
        push("activeColor", m_settings->scrollingTabIndicatorActiveColor());
        push("inactiveColor", m_settings->scrollingTabIndicatorInactiveColor());
        push("urgentColor", m_settings->scrollingTabIndicatorUrgentColor());
    }

    if (slot->isVisible() && !hideWasInFlight) {
        // Re-assert the surface's input state on EVERY update, not just when
        // the slot is first shown. The rects move whenever the strip relayouts
        // — which includes the relayout a tab click itself causes — and the
        // shell's own show/hide path flips Qt::WindowTransparentForInput
        // behind our back, so a region applied once at show time does not stay
        // applied. Skipping this made the indicator clickable exactly once:
        // the click switched tabs, the relayout that followed never re-applied
        // the region, and the strip stayed inert until something scrolled it
        // off screen and back, because only the re-show ran this.
        syncPassiveShellSurfaceStateForSurface(shellSurface);
        return; // live model update — no show choreography needed
    }
    // Either genuinely hidden, or mid-hide (still visible, opacity
    // animating toward 0). In BOTH cases the show choreography must run:
    // early-returning on a fading slot would leave it visible+loaded at
    // opacity 0 forever once the superseded hide completion no-ops.

    // Same user-font pipeline as every other overlay slot (OSD, cheatsheet,
    // picker): the pills must honour the overlay font family and size scale.
    // Pushed on the show path only, like all five siblings — a strip relayout
    // changes no font, so re-pushing six settings-derived properties on every
    // relayout was pure churn.
    writeFontProperties(slot, m_settings, /*includeLabelFontColor=*/false);

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
    m_surfaceAnimator->beginShow(shellSurface, slot, PhosphorRoles::ScrollTabs, []() { });
    // Re-derive the surface's input state after Surface::show() cleared the
    // transparent-input flag. The sync folds this slot's tab region into the
    // partial region it installs, so the indicator is clickable while the rest
    // of the passive shell stays click-through.
    syncPassiveShellSurfaceStateForSurface(shellSurface);
}

} // namespace PlasmaZones
