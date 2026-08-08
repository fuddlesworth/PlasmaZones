// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Bridge methods that wire OverlayService to PhosphorOverlay::ShellHost:
// the post-create / pre-destroy callbacks, the per-screen shim wrappers
// (ensurePassiveShellFor / destroyPassiveShell), the warm-up loop, and
// the surface-mapped-state sync that translates PZ-content slot
// visibility into the booleans ShellHost::syncSurfaceState expects.
//
// Extracted from osd.cpp where these methods accumulated during the
// Phase 2-4 ShellHost lift. They are not OSD-specific, they belong
// with the shell-host wiring conceptually.

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include "phosphor_roles.h"
#include "phosphor_slot_keys.h"

#include <PhosphorOverlay/ShellHost.h>

#include <PhosphorWayland/SurfaceIdentity.h>

#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include <PhosphorLayer/Surface.h>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QStringList>

namespace PlasmaZones {

// Once-only screenAdded hook: after the first warm-up sweep, every
// freshly-attached screen gets its passive overlay shell created so
// the OSD path is ready by the time the user fires their first
// hot-plug-induced shortcut. The m_notificationsWarmed flag gates
// whether new screens get a per-screen passive overlay shell after
// the initial warm-up; the effects-enabled predicate (shaders ||
// animations) applies the same lazy-create rule as the warm-up
// sweep, so hot-plug never proactively creates an always-on overlay
// surface when no transition needs the warm cache.
void OverlayService::ensureOsdScreenAddedConnected()
{
    if (m_screenAddedConnected) {
        return;
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        if (!m_notificationsWarmed) {
            return;
        }
        const bool shadersOn = m_shaderRegistry && m_shaderRegistry->shadersEnabled();
        const bool animationsOn = m_settings && m_settings->animationsEnabled();
        if (!shadersOn && !animationsOn) {
            return;
        }
        auto* mgr2 = m_screenManager;
        const QString physId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
        const QStringList ids = mgr2 ? mgr2->virtualScreenIdsFor(physId) : QStringList{physId};
        for (const QString& sid : ids) {
            ensurePassiveShellFor(sid, screen);
        }
    });
    m_screenAddedConnected = true;
}

OverlayService::PerScreenOverlayState* OverlayService::ensurePassiveShellFor(const QString& effectiveId,
                                                                             QScreen* physScreen)
{
    // Library-side lifecycle delegated to ShellHost. The SurfaceFactory /
    // PostCreate callbacks registered in the OverlayService ctor handle
    // the PZ-specific surface creation (role + qmlSource + warmed-surface
    // pipeline) and slot wiring; this shim caches the lib's stable
    // ShellState pointer on the daemon's PerScreenOverlayState so every
    // downstream consumer of `state->shell` reaches the lib's single
    // source of truth.
    auto* shellState = m_shellHost->ensureShell(effectiveId, physScreen);
    // Helper: enforce the "non-null shell pointer ⇒ live surface"
    // contract on either failure-return path by nulling any stale
    // cached pointer (a previous successful ensure may have wired
    // pState.shell to a ShellState whose fields have since been
    // zeroed by destroyShell). Callers gate on shell->shellSurface()
    // today, but keeping the cache true to the contract removes a
    // class of latent bugs.
    auto returnExistingClearingStaleShell = [this, &effectiveId]() -> PerScreenOverlayState* {
        auto it = m_screenStates.find(effectiveId);
        if (it == m_screenStates.end()) {
            return nullptr;
        }
        it->shell = nullptr;
        return &it.value();
    };
    if (!shellState) {
        return returnExistingClearingStaleShell();
    }
    // Surface-live gate: a zeroed state (sticky failure or self-
    // teardown from wirePassiveShellSlots's null-shellWindow recovery)
    // must not be cached on the daemon's parallel state, and the
    // defensive flag-set below must not touch a zombie window.
    if (!shellState->shellSurface()) {
        return returnExistingClearingStaleShell();
    }
    // Defensive default: every successful ensure should leave the
    // shell window click-through unless a modal slot is up. The
    // companion syncPassiveShellSurfaceState call is the authoritative
    // setter, but a missed sync (e.g. during screen power-cycle re-
    // entry where the lib returns the cached state without re-firing
    // PostCreate) would otherwise inherit a stale
    // wantTransparent=false from a prior modal popup. Asserting the
    // default here ensures the shell never silently steals clicks
    // when the daemon thinks no modal is active. Modal-show paths
    // overwrite this immediately via syncPassiveShellSurfaceState
    // (anyInputGrabbing=true), so the brief redundant write happens
    // entirely within a single event-loop tick.
    if (auto* window = shellState->shellWindow()) {
        window->setFlag(Qt::WindowTransparentForInput, true);
    }
    auto& pState = m_screenStates[effectiveId];
    pState.shell = shellState;
    return &pState;
}

OverlayService::PerScreenOverlayState* OverlayService::ensureScrollTabShellFor(const QString& effectiveId,
                                                                               QScreen* physScreen)
{
    // Same shape as ensurePassiveShellFor above, against the twin host, and
    // the contract it enforces is the same: a non-null cached pointer always
    // means a live surface.
    //
    // The passive shell's defensive Qt::WindowTransparentForInput assertion has
    // no twin here. It guards against a modal slot leaving a stale grab behind,
    // and this surface hosts no modal; the once-only assertion it does need
    // (before the first frame is primed) lives in wireScrollTabShellSlots.
    auto* shellState = m_tabShellHost->ensureShell(effectiveId, physScreen);
    auto returnExistingClearingStaleShell = [this, &effectiveId]() -> PerScreenOverlayState* {
        auto it = m_screenStates.find(effectiveId);
        if (it == m_screenStates.end()) {
            return nullptr;
        }
        it->tabShell = nullptr;
        return &it.value();
    };
    if (!shellState || !shellState->shellSurface()) {
        return returnExistingClearingStaleShell();
    }
    auto& pState = m_screenStates[effectiveId];
    pState.tabShell = shellState;
    return &pState;
}

void OverlayService::wireScrollTabShellSlots(const QString& screenId, PhosphorOverlay::ShellState& shellState)
{
    auto* window = shellState.shellWindow();
    if (!window) {
        // Same unrecoverable half-wired state ensureShell can leave behind on
        // the async QML path, and the same two-step recovery: tear the shell
        // down so the next ensure falls through to the failure-flag check, then
        // flag it so attempts no-op until a hot-plug clears it. See
        // wirePassiveShellSlots for the full reasoning.
        qCWarning(lcOverlay) << "wireScrollTabShellSlots: shellWindow null at PostCreate for screen=" << screenId
                             << "- tearing down half-wired shell and marking failure";
        m_tabShellHost->destroyShell(screenId);
        m_tabShellHost->markFailure(screenId);
        return;
    }

    auto* item = qvariant_cast<QQuickItem*>(window->property("scrollTabsSlotItem"));
    if (!item) {
        qCWarning(lcOverlay) << "ScrollTabShell on screen=" << screenId
                             << "did not expose `scrollTabsSlotItem`: scroll tab strips on this screen will fail."
                             << "Check QML resource.";
    } else {
        shellState.slots.insert(PhosphorSlotKeys::ScrollTabs(),
                                PhosphorOverlay::SlotEntry{item, PhosphorRoles::ScrollTabs});
    }

    // String-based SIGNAL/SLOT because the source is a QML object's dynamic
    // signal, which the function-pointer form cannot resolve at compile time.
    QObject::connect(window, SIGNAL(scrollTabActivated(QString)), this, SLOT(onScrollTabActivated(QString)));

    // Click-through, asserted AFTER the prime. A QQuickWindow starts without
    // the flag, and priming shows the surface to render its first frame —
    // Surface::show() clears WindowTransparentForInput, so setting it first
    // and priming second left the flag off exactly when the surface was up.
    // A screen-sized surface then took every click on the monitor until the
    // first strip update ran a sync, and on the path where that update bails
    // (no slot) it never ran one at all. The passive shell orders these the
    // same way round, for the same reason.
    primeSurfaceRenderPipeline(shellState.shellSurface());

    window->setFlag(Qt::WindowTransparentForInput, true);
}

void OverlayService::unwireScrollTabShellSlots(const QString& screenId)
{
    // The lib clears ShellState::slots after this hook, so only the daemon's
    // parallel bookkeeping is ours to drop. Same keep/drop split as
    // unwirePassiveShellSlots: m_scrollTabsHideGuard is monotonic and MUST
    // survive (a restarted counter lets a stale hide completion tear down a
    // repopulated slot), while the hide-pending bit, the cached model, the
    // input region and the paint overrides are plain state for a shell that no
    // longer exists and would otherwise grow by one dead screen per hot-plug.
    //
    // Of those, only the overrides are not self-healing: their sole writer is
    // Daemon::updateScrollingScreens, which runs on a rules/context re-resolve
    // rather than on a strip update, so a screen that stays in scrolling across
    // a teardown falls back to the config colours until the next such pass.
    //
    // The retraction goes FIRST, and that ordering is load-bearing: this hook
    // runs before the library schedules the surface for deletion, and Wayland
    // reuses object ids, so a registration outliving its surface can come to
    // name an unrelated one and the compositor would slide the wrong thing.
    announceScrollTabSurface(screenId, nullptr);
    m_scrollTabsHidePending.remove(screenId);
    // The strip model SURVIVES, unlike its neighbours. It is the only replay
    // source the daemon has, and both of its producers are change-latched: the
    // engine re-emits tabStripsChanged only on a structural change, and
    // replayScrollTabStrips walks this very map. Dropping it meant a screen
    // whose shells were destroyed and rebuilt — a monitor power-cycle is the
    // ordinary case — showed no indicators until the strip's STRUCTURE
    // happened to change, which for a settled workspace can be a long time.
    // Keeping it costs one entry per screen id ever seen and makes the replay
    // self-healing, since updateScrollTabStrips recreates the shell through
    // ensureScrollTabShellFor. Genuine screen removal drops it below.
    m_scrollTabInputRegions.remove(screenId);
    m_scrollTabIndicatorOverrides.remove(screenId);
    auto it = m_screenStates.find(screenId);
    if (it != m_screenStates.end()) {
        it->tabShell = nullptr;
    }
}

void OverlayService::wirePassiveShellSlots(const QString& screenId, PhosphorOverlay::ShellState& shellState)
{
    // Look up the per-content slot Items by their QML object names -
    // exposed as `osdSlotItem` / `snapAssistSlotItem` / … on the shell
    // window root via QML aliases - and cache them under the daemon's
    // slot-key vocabulary in the lib's generic slot map. Per-show
    // writeQmlProperty / animator beginShow target these Items via
    // the PerScreenOverlayState::xxxSlot() accessors.
    auto* window = shellState.shellWindow();
    if (!window) {
        // QML hasn't materialized the wrapper window yet (async path).
        // The lib's ensureShell already set shellState.m_shellSurface
        // before invoking this callback; if we only called markFailure
        // the next ensureShell would short-circuit on the live
        // shellSurface BEFORE consulting the failure flag and return
        // the same half-wired state forever.
        //
        // Tear down the shell synchronously (zeroes shellSurface so
        // the next call falls through to the failure-flag check) AND
        // mark the failure so subsequent attempts no-op until a
        // hot-plug clearFailure unblocks a clean re-create.
        qCWarning(lcOverlay) << "wirePassiveShellSlots: shellWindow null at PostCreate for screen=" << screenId
                             << "- tearing down half-wired shell and marking failure";
        m_shellHost->destroyShell(screenId);
        m_shellHost->markFailure(screenId);
        return;
    }

    auto wireSlot = [&](const QString& slotKey, const char* qmlObjectName, const PhosphorLayer::Role& role,
                        const char* descriptionForLog) {
        auto* item = qvariant_cast<QQuickItem*>(window->property(qmlObjectName));
        if (!item) {
            qCWarning(lcOverlay) << "PassiveOverlayShell on screen=" << screenId << "did not expose `" << qmlObjectName
                                 << "`:" << descriptionForLog << "will fail. Check QML resource.";
            // Skip the insert. With no entry under the key, hideSlot's
            // "no slot" path runs (which now fires completion synchronously)
            // rather than the "null item" path on a SlotEntry{nullptr, role}
            // - the absent-entry shape makes the breakage visible in
            // failure logs instead of silently looking like a successful hide.
            return;
        }
        shellState.slots.insert(slotKey, PhosphorOverlay::SlotEntry{item, role});
    };

    wireSlot(PhosphorSlotKeys::Osd(), "osdSlotItem", PhosphorRoles::Osd, "OSD content writes");
    wireSlot(PhosphorSlotKeys::SnapAssist(), "snapAssistSlotItem", PhosphorRoles::SnapAssist,
             "snap-assist on this screen");
    wireSlot(PhosphorSlotKeys::LayoutPicker(), "layoutPickerSlotItem", PhosphorRoles::LayoutPicker,
             "picker on this screen");
    wireSlot(PhosphorSlotKeys::ZoneSelector(), "zoneSelectorSlotItem", PhosphorRoles::ZoneSelector,
             "selector on this screen");
    wireSlot(PhosphorSlotKeys::MainOverlay(), "mainOverlaySlotItem", PhosphorRoles::ZoneOverlay,
             "main overlay on this screen");
    wireSlot(PhosphorSlotKeys::Cheatsheet(), "cheatsheetSlotItem", PhosphorRoles::Cheatsheet,
             "cheatsheet on this screen");
    wireSlot(PhosphorSlotKeys::ScrollDropIndicator(), "scrollDropIndicatorSlotItem", PhosphorRoles::ScrollDropIndicator,
             "scroll drag drop indicator on this screen");

    // Wire QML signals → animator-driven slot hide / forward.
    // String-based SIGNAL/SLOT macros are required here because the source
    // signals are declared on `PassiveOverlayShell.qml` (a QML object's
    // dynamic signal list), not on a C++ type - the function-pointer form
    // can't resolve them at compile time.
    QObject::connect(window, SIGNAL(osdDismissRequested()), this, SLOT(onOsdDismissRequested()));
    QObject::connect(window, SIGNAL(snapAssistDismissRequested()), this, SLOT(onSnapAssistDismissRequested()));
    QObject::connect(window, SIGNAL(snapAssistWindowSelected(QString, QString, QString)), this,
                     SLOT(onSnapAssistWindowSelected(QString, QString, QString)));
    QObject::connect(window, SIGNAL(layoutPickerSelected(QString)), this, SLOT(onLayoutPickerSelected(QString)));
    QObject::connect(window, SIGNAL(layoutPickerDismissRequested()), this, SLOT(onLayoutPickerDismissRequested()));
    QObject::connect(window, SIGNAL(cheatsheetDismissRequested()), this, SLOT(onCheatsheetDismissRequested()));
    // No zoneSelectorZoneSelected wiring: the zone-selector slot is input-
    // transparent by design and hit-testing runs in C++ via
    // updateSelectorPosition. ZoneSelectorContent's zone previews declare no
    // pointer handlers, so input-transparency holds by construction on the
    // QML side too.

    // Prime the wl_surface map + Vulkan swapchain init + first-frame
    // render so the very first user-triggered slot show doesn't race
    // the FBO capture used by shader-exclusive transitions.
    primeSurfaceRenderPipeline(shellState.shellSurface());
}

// Pre-create the per-screen passive overlay shell for every effective
// screen the manager knows about, then mark notifications warmed and
// ensure the screenAdded hot-plug hook is installed.
//
// Effects-enabled gate: the sole purpose of the prewarm is to prime
// the wl_surface map + RHI swapchain so the first user-triggered slot
// show does not race the FBO capture used by shader-exclusive
// transitions. When both shaders and animations are disabled there is
// no transition to race, so the prewarm sweep is skipped (the
// screenAdded hot-plug hook follows the same rule). Each slot
// consumer (snapassist / selector / osd / overlay) calls
// ensurePassiveShellFor on its show path, so the shell is still
// created lazily on first use - it just isn't sitting on the wlr
// OVERLAY layer composited over every frame when no slot is up.
void OverlayService::warmUpNotifications()
{
    const bool shadersOn = m_shaderRegistry && m_shaderRegistry->shadersEnabled();
    const bool animationsOn = m_settings && m_settings->animationsEnabled();
    const bool effectsEnabled = shadersOn || animationsOn;

    const QStringList effectiveIds = (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());
    int createdCount = 0;
    if (effectsEnabled) {
        for (const QString& sid : effectiveIds) {
            QScreen* physScreen = m_screenManager ? m_screenManager->physicalScreenFor(sid).qscreen
                                                  : Utils::findScreenAtPosition(QPoint(0, 0));
            if (physScreen) {
                auto* state = ensurePassiveShellFor(sid, physScreen);
                if (state && state->shell && state->shell->shellSurface()) {
                    ++createdCount;
                }
            }
        }
    }
    m_notificationsWarmed = true;
    if (effectsEnabled) {
        qCInfo(lcOverlay) << "Pre-warmed passive overlay shell windows for" << createdCount << "of"
                          << effectiveIds.size() << "effective screens";
    } else {
        qCInfo(lcOverlay) << "Skipping passive overlay shell prewarm: shaders and animations both disabled"
                          << "(" << effectiveIds.size() << "effective screens; lazy-create on first slot show)";
    }
    ensureOsdScreenAddedConnected();
}

void OverlayService::destroyPassiveShell(const QString& screenId)
{
    // Both of the screen's shells go together. Every teardown site names this
    // one function, so pairing them here is what keeps the tab shell from
    // outliving the screen it was built for — it has no teardown path of its
    // own, and a survivor would keep a layer surface mapped on a monitor that
    // is gone.
    m_tabShellHost->destroyShell(screenId);
    // Library-side teardown delegated to ShellHost::destroyShell. The
    // PreDestroy callback (wirePassiveShellSlots' inverse) clears every
    // PZ-content sentinel on the daemon's PerScreenOverlayState before
    // the lib schedules the shell surface for deletion; the lib then
    // nulls its own ShellState mechanism fields. The shell QQuickWindow
    // owns every slot QQuickItem as a scene-graph descendant so the
    // single deleteLater on the surface cascades through every slot.
    m_shellHost->destroyShell(screenId);
}

void OverlayService::unwirePassiveShellSlots(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it == m_screenStates.end()) {
        return;
    }
    // The lib's destroyShell clears ShellState::slots after this hook
    // runs, so no slot-pointer nulling is needed here. We only have to
    // clear the daemon's PZ-content sentinels and disconnect the geom
    // watcher - those are the parallel-state bookkeeping the lib does
    // not know about.
    //
    // The tab-indicator maps are NOT cleared here even though they are keyed
    // the same way: the indicators are not slots on this shell, and
    // unwireScrollTabShellSlots clears them when THEIR shell goes. Both are
    // torn down at the same call sites, so doing it in one place keeps the
    // clears paired with the shell whose destruction actually invalidates them.
    //
    // The drop indicator's two per-screen maps DO belong here. Its guard
    // follows the monotonic EXCEPTION: m_scrollDropIndicatorHideGuard must
    // never restart, so it stays. Dropping the rect cache
    // matters for more than the leak — it is the change gate, so a retained
    // entry would make an identical rect after a shell teardown compare equal
    // and early-return, and the indicator would silently never show again.
    m_scrollDropIndicatorHidePending.remove(screenId);
    m_lastScrollDropIndicatorRect.remove(screenId);
    // The paint overrides go too, matching the tab twin above. They belong to
    // the torn-down shell's context, and nothing else drops them.
    m_scrollDropIndicatorOverrides.remove(screenId);
    QObject::disconnect(it->overlayGeomConnection);
    it->overlayGeomConnection = {};
    it->overlayPhysScreen = nullptr;
    it->overlayGeometry = QRect();
    it->labelsTextureHash = 0;
    it->zoneSelectorPhysScreen = nullptr;
    it->zoneSelectorGeometry = QRect();
}

void OverlayService::announceScrollTabSurface(const QString& screenId, QQuickWindow* window)
{
    // Read the id LATE (from a shown window) rather than at shell creation:
    // the wl_surface only exists once the QWindow is realised, so an early
    // read would publish 0 and the indicators would never slide.
    const quint32 surfaceId = window ? PhosphorWayland::surfaceObjectId(window) : 0;
    const quint32 previous = m_scrollTabSurfaceIds.value(screenId, 0);
    if (surfaceId == previous) {
        return;
    }
    if (surfaceId == 0) {
        m_scrollTabSurfaceIds.remove(screenId);
    } else {
        m_scrollTabSurfaceIds.insert(screenId, surfaceId);
    }
    Q_EMIT scrollTabSurfaceChanged(screenId, surfaceId);
}

void OverlayService::removeShellStates(const QString& screenId)
{
    // destroyShell only zeroes a ShellState's fields; the entry itself survives
    // in the host's map, so a hot-plug cycle would slowly grow it with dead
    // keys. Both hosts share the key space, so both are dropped together.
    m_shellHost->removeState(screenId);
    m_tabShellHost->removeState(screenId);
    // This is the genuine-removal path — the screen's whole entry is going, not
    // just its surfaces — so the cached strip model goes with it. The shell
    // teardown hook deliberately keeps that cache so a rebuilt shell can replay
    // it; without this drop, the entry would outlive the screen itself.
    m_lastScrollTabStrips.remove(screenId);
}

void OverlayService::clearShellFailuresForPhysicalScreen(const QString& physicalScreenId)
{
    // Drop sticky creation-failure sentinels for every screen id rooted on this
    // physical monitor. Without this, reconnecting the same monitor inherits
    // the stale flag and we silently refuse to recreate its shells. Matching is
    // prefix-based because virtual-screen ids embed the physical id as their
    // prefix, and the bare physical id is checked for equality.
    if (physicalScreenId.isEmpty()) {
        return;
    }
    const QString vsPrefix = physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator;
    const auto clearOn = [&](PhosphorOverlay::ShellHost& host) {
        for (const QString& flagged : host.failureScreenIds()) {
            if (flagged == physicalScreenId || flagged.startsWith(vsPrefix)) {
                host.clearFailure(flagged);
            }
        }
    };
    clearOn(*m_shellHost);
    clearOn(*m_tabShellHost);
}

void OverlayService::syncPassiveShellSurfaceState(const QString& effectiveId)
{
    // Compute PZ-specific visibility predicates from the parallel slot
    // pointers on PerScreenOverlayState, then hand the resulting booleans
    // to the library. The lib decides the mapping + input-region toggle;
    // PZ decides what counts as "live" and "input-grabbing".
    //
    // Input-region rationale (Qt::WindowTransparentForInput): pre-shell-
    // migration each overlay had its own wl_surface sized to its visible
    // content, so clicks outside the toast / card naturally fell through
    // to underlying windows. Post-shell every kbd-None overlay shares the
    // screen-sized shell surface - there's no per-slot input region the
    // daemon can hand to the compositor. The pragmatic split: only MODAL
    // slots (snap-assist, layout picker, cheatsheet) grab input. OSD /
    // main overlay / zone-selector are purely visual:
    //   - OSDs auto-dismiss on a timer; a click-to-dismiss MouseArea
    //     inside the OSD content is the accepted casualty - the
    //     alternative is the daemon eating every click on the screen
    //     for the OSD's full lifetime, which the user reported as worse
    //     than losing click-dismiss. The OSD-card-over-modal variant of
    //     that casualty (an OSD firing while a modal is up would occlude
    //     modal content AND, since the modal grabs input, the card's
    //     MouseArea would eat clicks on its rect) no longer exists: the
    //     osdSlot's z binding in PassiveOverlayShell.qml drops the OSD
    //     below the modal slots (3 -> 1.5) whenever one is visible, so
    //     the modal both paints over and hit-tests before the card.
    //   - Main overlay during drag is driven by KWin's drag stream
    //     (cursor pushes via OverlayService::updateMousePosition);
    //     it never needs Qt-level input on its own.
    //   - Zone selector during drag is the same - D-Bus
    //     updateSelectorPosition pushes cursor coords; the zone is
    //     committed via drag-end-on-hovered-zone, not a Qt click.
    auto it = m_screenStates.constFind(effectiveId);
    if (it == m_screenStates.constEnd()) {
        return;
    }
    const auto& s = *it;
    auto isVisible = [](QQuickItem* item) {
        return item != nullptr && item->isVisible();
    };
    // anyVisible drives the wl_surface map/unmap edge. The main overlay
    // slot stays setVisible(true) for the entire drag (even across
    // modifier-trigger drag pauses set by setIdleForDragPause, which
    // only flip the slot's `_idled` property to blank the rendered
    // content) so a mid-drag trigger thrash does not churn the shell's
    // wl_surface map state. The slot transitions to !isVisible only on
    // real drag-end via dismissOverlayWindow - that is the right edge
    // for the shell to actually unmap when no other slot is up.
    const bool anyVisible = isVisible(s.osdSlot()) || isVisible(s.snapAssistSlot()) || isVisible(s.layoutPickerSlot())
        || isVisible(s.zoneSelectorSlot()) || isVisible(s.mainOverlaySlot()) || isVisible(s.cheatsheetSlot())
        || isVisible(s.scrollDropIndicatorSlot());
    const bool anyInputGrabbing =
        isVisible(s.snapAssistSlot()) || isVisible(s.layoutPickerSlot()) || isVisible(s.cheatsheetSlot());

    // The scroll tab indicators are absent from both predicates on purpose:
    // they are not slots on this shell at all. Their surface, its mapped state
    // and its partial input region are the tab shell's own business - see
    // syncScrollTabShellSurfaceState.
    m_shellHost->syncSurfaceState(effectiveId, anyVisible, anyInputGrabbing);

    // No tab-indicator gate here. Their surface is a sibling of this one on the
    // same layer and wlr-layer-shell cannot order two surfaces within a layer,
    // so the compositor would stack the later-created indicators ABOVE this
    // shell's modal cards. The effect settles it instead, lowering that surface
    // to the bottom of its layer once per map
    // (PlasmaZonesEffect::restackScrollTabSurfaces), which restores exactly the
    // tiers the two had while they shared one surface.
}

void OverlayService::syncScrollTabShellSurfaceState(const QString& effectiveId)
{
    auto it = m_screenStates.constFind(effectiveId);
    if (it == m_screenStates.constEnd()) {
        return;
    }
    QQuickItem* slot = it->scrollTabsSlot();
    const bool visible = slot != nullptr && slot->isVisible();

    // The indicators take clicks ONLY where they draw. A tabbed column's
    // indicator is a few pixels of a mostly-empty screen, so grabbing the whole
    // surface for it would eat every click on the desktop for as long as any
    // column stayed tabbed. Absent or hidden, the region is empty and the
    // surface is click-through.
    //
    // anyInputGrabbing is always false here: nothing on this surface is modal,
    // and passing true would make the library ignore the partial region and
    // hand the indicators every click on the screen.
    const QRegion tabRegion = visible ? m_scrollTabInputRegions.value(effectiveId) : QRegion();
    m_tabShellHost->syncSurfaceState(effectiveId, visible, /*anyInputGrabbing=*/false, tabRegion);
}

void OverlayService::syncPassiveShellSurfaceStateForSurface(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.value().shell && it.value().shell->shellSurface() == surface) {
            syncPassiveShellSurfaceState(it.key());
            return;
        }
    }
}

} // namespace PlasmaZones
