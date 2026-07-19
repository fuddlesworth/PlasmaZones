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
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "phosphor_roles.h"
#include "phosphor_slot_keys.h"

#include <PhosphorOverlay/ShellHost.h>

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
    QObject::disconnect(it->overlayGeomConnection);
    it->overlayGeomConnection = {};
    it->overlayPhysScreen = nullptr;
    it->overlayGeometry = QRect();
    it->labelsTextureHash = 0;
    it->zoneSelectorPhysScreen = nullptr;
    it->zoneSelectorGeometry = QRect();
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
    // slots (snap-assist, layout picker) grab input. OSD / main overlay /
    // zone-selector are purely visual:
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
        || isVisible(s.zoneSelectorSlot()) || isVisible(s.mainOverlaySlot()) || isVisible(s.cheatsheetSlot());
    const bool anyInputGrabbing =
        isVisible(s.snapAssistSlot()) || isVisible(s.layoutPickerSlot()) || isVisible(s.cheatsheetSlot());

    m_shellHost->syncSurfaceState(effectiveId, anyVisible, anyInputGrabbing);
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
