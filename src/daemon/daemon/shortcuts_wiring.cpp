// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Daemon::connectShortcutSignals — the ShortcutManager signal wiring, split
// out of start.cpp by concern (the file had grown past the size ceiling).
// Everything here runs once at startup from Daemon::start().

#include "daemon/daemon.h"

#include "config/settings.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "daemon/daemon/helpers.h"
#include "daemon/overlayservice.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "core/types/constants.h"
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>

namespace PlasmaZones {

void Daemon::connectShortcutSignals()
{
    // NOTE: registerShortcuts() is called by Daemon::start() before this method.
    // Do NOT call it again here — it would hit registerShortcuts()'s own
    // already-registered / in-flight guards and do nothing useful.

    // Connect shortcut signals
    // Screen detection: On X11, QCursor::pos() works; on Wayland, background daemons
    // get stale cursor data. resolveShortcutScreenId() handles both by falling back to
    // the screen reported by the KWin effect's windowActivated D-Bus call.
    connect(m_shortcutManager.get(), &ShortcutManager::openSettingsRequested, this, []() {
        // Scrub the GPU-preference variables this daemon exported (published
        // as an app property by daemon main): the settings app spawns the
        // editor later, and an inherited DRI_PRIME / QT_VK_PHYSICAL_DEVICE_INDEX
        // reaching that editor would trip its pre-set-value guards and freeze
        // it on this daemon's stale pin. systemd-run --scope runs the command
        // as a child of this call, so the environment set here is what the
        // settings app inherits.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (QCoreApplication::instance()) {
            const QStringList gpuVars = QCoreApplication::instance()->property(PGpuExportedVarsProperty).toStringList();
            for (const QString& var : gpuVars) {
                env.remove(var);
            }
            // Restore variables this process cleared for its own rendering —
            // the child must see the user's session value.
            const QVariantMap cleared = QCoreApplication::instance()->property(PGpuClearedVarsProperty).toMap();
            for (auto it = cleared.constBegin(); it != cleared.constEnd(); ++it) {
                env.insert(it.key(), it.value().toString());
            }
        }
        // Launch in its own systemd scope so stopping the daemon service
        // doesn't kill the settings app (they'd share a cgroup otherwise).
        QProcess scoped;
        scoped.setProgram(QStringLiteral("systemd-run"));
        scoped.setArguments(
            {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("plasmazones-settings")});
        scoped.setProcessEnvironment(env);
        if (!scoped.startDetached()) {
            // Fallback if systemd-run is unavailable
            QProcess direct;
            direct.setProgram(QStringLiteral("plasmazones-settings"));
            direct.setProcessEnvironment(env);
            if (!direct.startDetached()) {
                qCWarning(lcDaemon) << "Failed to launch plasmazones-settings";
            }
        }
    });
    connect(m_shortcutManager.get(), &ShortcutManager::openEditorRequested, this, [this]() {
        // Screen-targeted (edits a screen's layout) — resolve cursor-first.
        // See layoutPickerRequested below for the rationale.
        QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty() && m_unifiedLayoutController) {
            screenId = m_unifiedLayoutController->currentScreenName();
        }
        if (!screenId.isEmpty()) {
            // Pass the effective screen ID directly — the editor handles both
            // physical and virtual screen IDs (VS-aware since v2.9).
            m_layoutAdaptor->openEditorForScreen(screenId);
        } else {
            m_layoutAdaptor->openEditor();
        }
    });
    // Quick layout shortcuts (Meta+Alt+1-9). Quick slots are per slot ARRAY:
    // in snapping mode the slot holds a zone-layout UUID, in autotile mode an
    // autotile algorithm ID, and scrolling its own array of native template
    // ids. Resolve the cursor screen's current mode, look up that mode's
    // slot, and apply the explicitly-bound entry — NOT the Nth entry in
    // priority order.
    connect(m_shortcutManager.get(), &ShortcutManager::quickLayoutRequested, this, [this](int number) {
        if (!m_unifiedLayoutController || !m_layoutManager) {
            return;
        }
        // Screen-targeted (applies a layout to a screen) — resolve
        // cursor-first. See layoutPickerRequested below for the rationale.
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "QuickLayout shortcut: no screen info";
            return;
        }
        // Quick slots need an engine with a layout concept. On a Templates
        // screen (scrolling) the slot names a native template and applies
        // through applyEntry's template branch; only a capability-less
        // engine answers with feedback.
        if (layoutSupportForScreen(screenId) == LayoutSupport::None) {
            showLayoutsUnavailableOsd(screenId);
            return;
        }
        const PhosphorZones::AssignmentEntry::Mode mode = currentModeFor(screenId);
        const QString slotId = m_layoutManager->quickLayoutSlots(mode).value(number);
        if (slotId.isEmpty()) {
            // Explicitly-unbound slot for this mode — a deliberate no-op,
            // never a fallback to priority order.
            qCDebug(lcDaemon) << "QuickLayout shortcut: slot" << number << "unset for mode" << mode;
            return;
        }
        m_unifiedLayoutController->setCurrentScreenName(screenId);
        // Push the LIVE capability so applyEntry's template branch routes on
        // the engine that actually owns the screen, not the cascade alone.
        m_unifiedLayoutController->setCurrentLayoutSupport(layoutSupportForScreen(screenId));
        if (isScreenLockedForLayoutChange(screenId)) {
            return;
        }
        // Filter the controller's layout list to the SAME mode we resolved the
        // slot from, so the bound slot ID (manual UUID in snapping, autotile
        // algorithm in autotile) is always in scope for applyLayoutById. Deriving
        // the filter from `mode` here — rather than re-resolving the screen's mode
        // via updateLayoutFilterForScreen, which reads the assignment cascade —
        // keeps the slot lookup and the filter on one source of truth
        // (currentModeFor). The two can otherwise disagree when the live engine is
        // autotile-active via a per-screen override the cascade doesn't carry,
        // which would leave applyLayoutById unable to find the autotile slot.
        // applyLayoutById routes through applyEntry, which handles both manual
        // assignment and autotile algorithm switching.
        const bool autotile = (mode == PhosphorZones::AssignmentEntry::Autotile);
        const bool scrolling = (mode == PhosphorZones::AssignmentEntry::Scrolling);
        m_unifiedLayoutController->setLayoutFilter(!autotile && !scrolling, autotile, scrolling);
        if (!m_unifiedLayoutController->applyLayoutById(slotId)) {
            return;
        }
        resnapIfManualMode();
    });

    // Cycle layout shortcuts (Meta+Alt+[ / Meta+Alt+])
    connect(m_shortcutManager.get(), &ShortcutManager::previousLayoutRequested, this, [this]() {
        if (m_cycleLayoutDebounce.isValid() && m_cycleLayoutDebounce.elapsed() < kShortcutDebounceMs) {
            return;
        }
        // Screen-targeted (cycles a screen's layout) — resolve cursor-first.
        // See layoutPickerRequested below for the rationale.
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "PreviousLayout shortcut: no screen info";
            return;
        }
        // Restart once a screen resolves — handleCycleLayout's own refusal
        // paths (the layouts-unavailable OSD on a non-layout engine, and the
        // locked-preview OSD on a locked screen) both count as the dispatch
        // this window throttles (unlike handleSpan's guards, which reject
        // with no user-visible effect).
        m_cycleLayoutDebounce.restart();
        handleCycleLayout(screenId, false);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::nextLayoutRequested, this, [this]() {
        if (m_cycleLayoutDebounce.isValid() && m_cycleLayoutDebounce.elapsed() < kShortcutDebounceMs) {
            return;
        }
        // Screen-targeted (cycles a screen's layout) — resolve cursor-first.
        // See layoutPickerRequested below for the rationale.
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "NextLayout shortcut: no screen info";
            return;
        }
        // Restart once a screen resolves — handleCycleLayout's own refusal
        // paths (the layouts-unavailable OSD on a non-layout engine, and the
        // locked-preview OSD on a locked screen) both count as the dispatch
        // this window throttles (unlike handleSpan's guards, which reject
        // with no user-visible effect).
        m_cycleLayoutDebounce.restart();
        handleCycleLayout(screenId, true);
    });

    // ═══════════════════════════════════════════════════════════════════════════
    // Keyboard Navigation Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    // Navigation shortcuts — single code path per operation (handleXxx)
    connect(m_shortcutManager.get(), &ShortcutManager::moveWindowRequested, this, [this](NavigationDirection d) {
        handleMove(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::spanWindowRequested, this, [this](NavigationDirection d) {
        handleSpan(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::focusZoneRequested, this, [this](NavigationDirection d) {
        handleFocus(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::pushToEmptyZoneRequested, this, [this]() {
        handlePush();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::restoreWindowSizeRequested, this, [this]() {
        handleRestore();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::toggleWindowFloatRequested, this, [this]() {
        handleFloat();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::swapWindowRequested, this, [this](NavigationDirection d) {
        handleSwap(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::rotateWindowsRequested, this, [this](bool cw) {
        handleRotate(cw);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::swapVirtualScreenRequested, this, [this](NavigationDirection d) {
        handleSwapVirtualScreen(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::rotateVirtualScreensRequested, this, [this](bool cw) {
        handleRotateVirtualScreens(cw);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::snapToZoneRequested, this, [this](int n) {
        handleSnap(n);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::cycleWindowsInZoneRequested, this, [this](bool fwd) {
        handleCycle(fwd);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::resnapToNewLayoutRequested, this, [this]() {
        handleResnap();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::snapAllWindowsRequested, this, [this]() {
        handleSnapAll();
    });

    // PhosphorZones::Layout picker shortcut (interactive layout browser + resnap)
    // Capture screen name at open time so it's still valid after the picker closes.
    //
    // Escape handling: KWin's wlr-layer-shell does not deliver keyboard
    // events to the picker's QQuickWindow on this Qt/KDE combination
    // (verified via Keys.onPressed diagnostic — fires zero times for the
    // duration of the picker). The QML Shortcut path is therefore unable
    // to react to Escape. We register Escape via KGlobalAccel using the
    // SAME id as the drag-cancel shortcut (`kCancelOverlayId`) so that:
    //   1. KGlobalAccel doesn't see two distinct actions competing for
    //      Escape — it only routes to one action per key, and the second
    //      ad-hoc registration would otherwise be silently no-op'd.
    //   2. cancelSnap() — the kCancelOverlayId callback — already
    //      dismisses whichever overlay is visible; the picker-takes-
    //      precedence ordering lives there.
    connect(m_shortcutManager.get(), &ShortcutManager::layoutPickerRequested, this, [this]() {
        if (!m_unifiedLayoutController) {
            return;
        }
        // The layout picker is screen-targeted — it picks the layout for a
        // screen — not window-targeted. Resolve cursor-first: the user's
        // intent is "the screen I am looking at". resolveShortcutScreenId
        // (focused-window-first) misroutes the picker to the wrong virtual
        // screen when the cursor rests on a different VS than the focused
        // window (e.g. just dropped a window on vs:1 while the focused
        // window is still on vs:0).
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "LayoutPicker shortcut: no screen info";
            return;
        }
        // The picker opens for any engine with a layout concept: Placement
        // screens pick a placement layout, Templates screens (scrolling)
        // pick the sizing template — the apply path routes per mode, so the
        // picker is no longer an exit door out of scrolling. Only a
        // capability-less engine gets feedback.
        if (layoutSupportForScreen(screenId) == LayoutSupport::None) {
            showLayoutsUnavailableOsd(screenId);
            return;
        }
        // At most one Escape-consuming modal at a time — the cheatsheet's
        // dedicated Escape grab would key-conflict with the picker's shared
        // cancel-overlay grab (KGlobalAccel routes one action per key).
        // Dismissing first releases the cheatsheet grab synchronously.
        m_overlayService->hideCheatsheet();
        m_unifiedLayoutController->setCurrentScreenName(screenId);
        // Live capability for applyEntry's template routing — see the
        // quick-slot handler above.
        m_unifiedLayoutController->setCurrentLayoutSupport(layoutSupportForScreen(screenId));
        updateLayoutFilterForScreen(screenId);
        // An empty candidate list (allow-lists plus the aspect filter can
        // wipe it on a screen with no current selection) makes the picker's
        // show bail silently — answer with the unavailable OSD instead of
        // nothing.
        if (m_overlayService->visibleLayoutCount(screenId) == 0) {
            showLayoutsUnavailableOsd(screenId);
            return;
        }
        m_overlayService->showLayoutPicker(screenId);
        // Bind the picker's KGlobalAccel grabs only if the picker actually
        // became visible. showLayoutPicker() bails without setting
        // m_layoutPickerVisible when the screen, shell, or layout list is
        // missing, and the only releaser of the nav grabs is
        // layoutPickerDismissed, which never fires for an invisible picker —
        // so binding on a failed show would leak the Escape + arrow/Return/Enter
        // grabs system-wide (swallowed even over fullscreen games) until the
        // next successful open+dismiss or a compositor reconnect. Re-press of an
        // already-visible picker is fine: isLayoutPickerVisible() is true and
        // registration is idempotent.
        if (m_windowDragAdaptor && m_overlayService->isLayoutPickerVisible()) {
            m_windowDragAdaptor->ensureCancelOverlayShortcutRegistered();
            // Picker navigation accels — registered while picker is
            // shown, dropped on dismiss. The unified PassiveOverlayShell
            // is kbd-None so the QML Shortcuts in LayoutPickerContent
            // can't fire; routing via KGlobalAccel is the replacement.
            // Lambdas capture the long-lived OverlayService — outlives
            // the registration window.
            auto* svc = m_overlayService.get();
            m_windowDragAdaptor->ensureLayoutPickerNavShortcutsRegistered(
                [svc](int dx, int dy) {
                    svc->pickerMoveSelection(dx, dy);
                },
                [svc] {
                    svc->pickerConfirmSelection();
                });
        }
    });
    // Snap-assist Escape: the unified PassiveOverlayShell is kbd-None
    // (the legacy SnapAssistOverlay's kbd-Exclusive QML Shortcut for
    // Escape no longer exists), so we register the global Escape
    // accelerator on every snap-assist show — cancelSnap() routes
    // Escape to hideSnapAssist() via the existing
    // isSnapAssistVisible() branch in WindowDragAdaptor::cancelSnap.
    // The matching unregister fires on snapAssistDismissed via
    // WindowDragAdaptor::onSnapAssistDismissed.
    connect(m_overlayService.get(), &IOverlayService::snapAssistShown, this,
            [this](const QString&, const PhosphorProtocol::EmptyZoneList&,
                   const PhosphorProtocol::SnapAssistCandidateList&) {
                // Same one-Escape-consumer contract as the picker path: the
                // cheatsheet's dedicated grab must be gone before the shared
                // cancel-overlay grab registers, or Escape stays routed to a
                // dismissed sheet.
                m_overlayService->hideCheatsheet();
                if (m_windowDragAdaptor) {
                    m_windowDragAdaptor->ensureCancelOverlayShortcutRegistered();
                }
            });

    // Shortcut cheatsheet: toggle on the cursor screen; Escape grab is
    // bound inside toggleCheatsheet (only on a successful show) and
    // released on ANY dismissal path via cheatsheetDismissed.
    connect(m_shortcutManager.get(), &ShortcutManager::toggleCheatsheetRequested, this, [this]() {
        toggleCheatsheet();
    });
    connect(m_overlayService.get(), &OverlayService::cheatsheetDismissed, this, [this]() {
        onCheatsheetDismissed();
    });
    // Live refilter while the sheet is open: catalog changes (rebinds,
    // external System Settings edits), per-screen mode switches, and the
    // global autotile feature gate all re-push into the visible slot.
    // Each refresh re-resolves the mode for the sheet's BOUND screen, so
    // over-triggering is safe and cheap (no-op when hidden).
    connect(m_shortcutManager.get(), &ShortcutManager::cheatsheetModelChanged, this, [this]() {
        refreshCheatsheetIfVisible();
    });
    // The controller-driven mode-refresh connects (layoutApplied /
    // autotileApplied → refreshCheatsheetIfVisible) live in
    // connectLayoutSignals(): this function runs BEFORE
    // initializeUnifiedController() creates the controller, so a connect
    // guarded on m_unifiedLayoutController here would never fire.
    if (m_settings) {
        // Tracked handle: m_settings is deliberately excluded from stop()'s
        // per-sender sweep (its ctor/init connections must survive), so this
        // per-start connection is severed individually.
        m_perStartConnections.append(connect(m_settings.get(), &Settings::autotileEnabledChanged, this, [this]() {
            refreshCheatsheetIfVisible();
        }));
        // Scrolling needs the same twin now that it is a first-class
        // cheatsheet mode: the sheet filters its rows on the resolved mode,
        // and disabling the master switch while it is open otherwise leaves
        // the Scrolling group on screen for a mode that is no longer live.
        m_perStartConnections.append(connect(m_settings.get(), &Settings::scrollingEnabledChanged, this, [this]() {
            refreshCheatsheetIfVisible();
        }));
    }
    connect(m_overlayService.get(), &OverlayService::layoutPickerDismissed, this, [this]() {
        // Release the shared Escape grab only when no other consumer (snap
        // assist) still needs it — releaseCancelOverlayShortcutIfIdle() is the
        // canonical cross-consumer guard (the picker is already hidden by the
        // time this fires). The 6 picker-nav grabs are picker-only, so always
        // drop them.
        if (m_windowDragAdaptor) {
            m_windowDragAdaptor->releaseCancelOverlayShortcutIfIdle();
            m_windowDragAdaptor->releaseLayoutPickerNavShortcuts();
        }
    });
    connect(m_overlayService.get(), &OverlayService::layoutPickerSelected, this,
            [this](const QString& layoutId, const QString& pickerScreenId) {
                if (!m_unifiedLayoutController) {
                    return;
                }
                // Re-bind the controller to the screen the picker was BOUND to when
                // the pick was made: currentScreenName is a single mutable slot that
                // syncModeFromAssignments retargets on every desktop or activity
                // switch, so without the re-bind a mid-pick switch applied the pick
                // to whatever screen last grabbed the slot.
                if (!pickerScreenId.isEmpty() && pickerScreenId != m_unifiedLayoutController->currentScreenName()) {
                    m_unifiedLayoutController->setCurrentScreenName(pickerScreenId);
                }
                // Check if screen is locked for its current mode. Route through
                // the resolver's `handleFor(screenId)` — it composes the live
                // (mode, desktop, activity) tuple via the bound IModeProvider /
                // IWorkspaceState adapters, so this site stops re-stitching the
                // 3-step cascade the resolver was introduced to collapse.
                QString screenId =
                    pickerScreenId.isEmpty() ? m_unifiedLayoutController->currentScreenName() : pickerScreenId;
                if (!screenId.isEmpty() && m_contextResolver) {
                    if (m_contextResolver->isLocked(m_contextResolver->handleFor(screenId))) {
                        showLockedPreviewOsd(screenId);
                        return;
                    }
                }
                // Capability re-check at APPLY time: the picker cannot open on a
                // capability-less screen (gated at request time, and an empty list
                // bails the show), but a KCM apply, rule reconcile or per-screen
                // desktop switch can strip the bound screen's capability while the
                // picker sits open. A Placement↔Templates flip mid-pick is fine —
                // applyEntry re-resolves the mode and routes accordingly.
                if (!screenId.isEmpty() && layoutSupportForScreen(screenId) == LayoutSupport::None) {
                    showLayoutsUnavailableOsd(screenId);
                    return;
                }
                // Screen name was re-bound above from the picker's own bound screen;
                // RE-push the live capability at apply time too — a KCM apply or
                // rule reconcile can flip the screen's engine while the picker sits
                // open.
                if (!screenId.isEmpty()) {
                    m_unifiedLayoutController->setCurrentLayoutSupport(layoutSupportForScreen(screenId));
                }
                if (!m_unifiedLayoutController->applyLayoutById(layoutId)) {
                    return;
                }
                resnapIfManualMode();
            });

    // Toggle layout lock shortcut — locks/unlocks current screen at screen-level for current mode
    connect(m_shortcutManager.get(), &ShortcutManager::toggleLayoutLockRequested, this, [this]() {
        // Screen-targeted (locks a screen's layout) — resolve cursor-first.
        // See layoutPickerRequested above for the rationale.
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty() || !m_settings || !m_contextResolver) {
            return;
        }
        // Layout lock pins a screen's layout choice (its template, on a
        // Templates screen) — nothing to pin only when the engine has no
        // layout concept at all.
        if (layoutSupportForScreen(screenId) == LayoutSupport::None) {
            showLayoutsUnavailableOsd(screenId);
            return;
        }
        // Read the live mode through the resolver's frozen snapshot so this
        // site stops re-stitching (modeForScreen + Utils::contextLockKey)
        // — the resolver already composes the same Mode-typed lock key
        // internally via DaemonSettingsGateAdapter. We only need the
        // wire-encoded `key` here for the existing settings.setScreenLocked
        // mutation path, so the cast-and-compose stays — but the mode it
        // derives from is the resolver's authoritative value.
        const auto handle = m_contextResolver->handleFor(screenId);
        const int mode = static_cast<int>(handle.mode);
        QString key = Utils::contextLockKey(mode, screenId);
        // Lock at screen-level (desktop=0, activity="") so it applies to all desktops/activities
        // and matches the KCM's screen-level lock button
        bool wasLocked = m_settings->isScreenLocked(key);
        // Block settingsChanged during mutation — the signal triggers a
        // D-Bus relay, and external consumers (settings app) read from disk.
        // If the signal fires before save(), they read stale data.
        {
            QSignalBlocker blocker(m_settings.get());
            m_settings->setScreenLocked(key, !wasLocked);
        }
        m_settings->save();
        // Now that the file is written, notify external consumers
        if (m_settingsAdaptor) {
            Q_EMIT m_settingsAdaptor->settingsChanged();
        }

        // Templates screens resolve their native TEMPLATE for the unlock
        // card: resolveLayoutForScreen is the snap-only chain and would
        // announce an unrelated fallback snap layout there. The lock branch
        // needs no split — showLockedPreviewOsd is itself template-aware and
        // falls back to the text card when the context has no template.
        if (wasLocked) {
            if (mode == static_cast<int>(PhosphorZones::AssignmentEntry::Scrolling)) {
                const PhosphorZones::ScrollingTemplate templ = m_layoutManager->scrollingTemplateForContext(
                    screenId, currentDesktopForScreen(screenId), currentActivity());
                if (templ.isValid()) {
                    showScrollingTemplateOsd(templ, screenId);
                }
            } else if (PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId)) {
                showLayoutOsd(layout, screenId);
            }
        } else {
            showLockedPreviewOsd(screenId);
        }
        qCInfo(lcDaemon) << "Toggle layout lock:" << (wasLocked ? "unlocked" : "locked") << "screen=" << screenId
                         << "mode=" << mode;
    });
}

} // namespace PlasmaZones
