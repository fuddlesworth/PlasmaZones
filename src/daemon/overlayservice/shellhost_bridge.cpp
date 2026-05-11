// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Bridge methods that wire OverlayService to PhosphorOverlay::ShellHost:
// the post-create / pre-destroy callbacks, the per-screen shim wrappers
// (ensurePassiveShellFor / destroyPassiveShell), the warm-up loop, and
// the surface-mapped-state sync that translates PZ-content slot
// visibility into the booleans ShellHost::syncSurfaceState expects.
//
// Extracted from osd.cpp where these methods accumulated during the
// Phase 2-4 ShellHost lift. They are not OSD-specific — they belong
// with the shell-host wiring conceptually — and keep osd.cpp under
// the <800-line cap.

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "pz_roles.h"
#include "pz_slot_keys.h"

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
// the initial warm-up.
void OverlayService::ensureOsdScreenAddedConnected()
{
    if (m_screenAddedConnected) {
        return;
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        if (!m_notificationsWarmed) {
            return;
        }
        auto* mgr2 = m_screenManager;
        const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
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
    if (!shellState) {
        auto it = m_screenStates.find(effectiveId);
        return (it == m_screenStates.end()) ? nullptr : &it.value();
    }
    auto& pzState = m_screenStates[effectiveId];
    pzState.shell = shellState;
    return &pzState;
}

void OverlayService::wirePassiveShellSlots(const QString& screenId, PhosphorOverlay::ShellState& shellState)
{
    // Look up the per-content slot Items by their QML object names —
    // exposed as `osdSlotItem` / `snapAssistSlotItem` / … on the shell
    // window root via QML aliases — and cache them under the daemon's
    // slot-key vocabulary in the lib's generic slot map. Per-show
    // writeQmlProperty / animator beginShow target these Items via
    // the PerScreenOverlayState::xxxSlot() accessors.
    auto* window = shellState.shellWindow;
    if (!window) {
        return;
    }

    auto wireSlot = [&](const QString& slotKey, const char* qmlObjectName, const PhosphorLayer::Role& role,
                        const char* descriptionForLog) {
        auto* item = qvariant_cast<QQuickItem*>(window->property(qmlObjectName));
        if (!item) {
            qCWarning(lcOverlay) << "PassiveOverlayShell on screen=" << screenId << "did not expose `" << qmlObjectName
                                 << "`:" << descriptionForLog << "will fail. Check QML resource.";
        }
        shellState.slots.insert(slotKey, PhosphorOverlay::SlotEntry{item, role});
    };

    wireSlot(PzSlotKeys::Osd(), "osdSlotItem", PzRoles::Osd, "OSD content writes");
    wireSlot(PzSlotKeys::SnapAssist(), "snapAssistSlotItem", PzRoles::SnapAssist, "snap-assist on this screen");
    wireSlot(PzSlotKeys::LayoutPicker(), "layoutPickerSlotItem", PzRoles::LayoutPicker, "picker on this screen");
    wireSlot(PzSlotKeys::ZoneSelector(), "zoneSelectorSlotItem", PzRoles::ZoneSelector, "selector on this screen");
    wireSlot(PzSlotKeys::MainOverlay(), "mainOverlaySlotItem", PzRoles::ZoneOverlay, "main overlay on this screen");

    // Wire QML signals → animator-driven slot hide / forward.
    QObject::connect(window, SIGNAL(osdDismissRequested()), this, SLOT(onOsdDismissRequested()));
    QObject::connect(window, SIGNAL(snapAssistDismissRequested()), this, SLOT(onSnapAssistDismissRequested()));
    QObject::connect(window, SIGNAL(snapAssistWindowSelected(QString, QString, QString)), this,
                     SLOT(onSnapAssistWindowSelected(QString, QString, QString)));
    QObject::connect(window, SIGNAL(layoutPickerSelected(QString)), this, SLOT(onLayoutPickerSelected(QString)));
    QObject::connect(window, SIGNAL(layoutPickerDismissRequested()), this, SLOT(onLayoutPickerDismissRequested()));
    QObject::connect(window, SIGNAL(zoneSelectorZoneSelected(QString, int, QVariant)), this,
                     SLOT(onZoneSelected(QString, int, QVariant)));

    // Prime the wl_surface map + Vulkan swapchain init + first-frame
    // render so the very first user-triggered slot show doesn't race
    // the FBO capture used by shader-exclusive transitions.
    primeSurfaceRenderPipeline(shellState.shellSurface);
}

// Pre-create the per-screen passive overlay shell for every effective
// screen the manager knows about, then mark notifications warmed and ensure
// the screenAdded hot-plug hook is installed.
void OverlayService::warmUpNotifications()
{
    const QStringList effectiveIds = (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());
    int createdCount = 0;
    for (const QString& sid : effectiveIds) {
        QScreen* physScreen =
            m_screenManager ? m_screenManager->physicalQScreenFor(sid) : Utils::findScreenAtPosition(QPoint(0, 0));
        if (physScreen) {
            auto* state = ensurePassiveShellFor(sid, physScreen);
            if (state && state->shell->shellSurface) {
                ++createdCount;
            }
        }
    }
    m_notificationsWarmed = true;
    qCInfo(lcOverlay) << "Pre-warmed passive overlay shell windows for" << createdCount << "of" << effectiveIds.size()
                      << "effective screens";
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
    // watcher — those are the parallel-state bookkeeping the lib does
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
    // screen-sized shell surface — there's no per-slot input region the
    // daemon can hand to the compositor. The pragmatic split: only MODAL
    // slots (snap-assist, layout picker) grab input. OSD / main overlay /
    // zone-selector are purely visual:
    //   - OSDs auto-dismiss on a timer; a click-to-dismiss MouseArea
    //     inside the OSD content is the accepted casualty — the
    //     alternative is the daemon eating every click on the screen
    //     for the OSD's full lifetime, which the user reported as worse
    //     than losing click-dismiss.
    //   - Main overlay during drag is driven by KWin's drag stream
    //     (cursor pushes via OverlayService::updateMousePosition);
    //     it never needs Qt-level input on its own.
    //   - Zone selector during drag is the same — D-Bus
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
    // Main overlay slot stays setVisible(true) across drag-pause idles
    // (setIdleForDragPause / applyIdleStateForCursor) so the warm RHI
    // pipeline survives mid-drag trigger thrashing. During those idles
    // the slot's `_idled` property is true and the inner content's
    // `visible: !root._idled` binding makes the rendered subtree invisible
    // — but the slot Item itself stays Qt-visible. Treat `_idled` slots
    // as not visible for the surface-show predicate.
    auto isMainOverlayLive = [](QQuickItem* slot) {
        if (!slot || !slot->isVisible()) {
            return false;
        }
        return !slot->property("_idled").toBool();
    };
    const bool anyVisible = isVisible(s.osdSlot()) || isVisible(s.snapAssistSlot()) || isVisible(s.layoutPickerSlot())
        || isVisible(s.zoneSelectorSlot()) || isMainOverlayLive(s.mainOverlaySlot());
    const bool anyInputGrabbing = isVisible(s.snapAssistSlot()) || isVisible(s.layoutPickerSlot());

    m_shellHost->syncSurfaceState(effectiveId, anyVisible, anyInputGrabbing);
}

void OverlayService::syncPassiveShellSurfaceStateForSurface(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.value().shell->shellSurface == surface) {
            syncPassiveShellSurfaceState(it.key());
            return;
        }
    }
}

} // namespace PlasmaZones
