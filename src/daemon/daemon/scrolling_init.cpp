// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — scrolling-mode shortcut wiring
//
// Connects the ShortcutManager's scroll-specific signals to the concrete
// ScrollEngine — the column vocabulary (including the group-width verbs,
// equalize and minimize), the view page pan, the edge-stop/wrap focus
// variants, the top/bottom window focus, and the one-way float verbs. The
// GENERIC directional move/focus/swap chords and the mode-neutral
// floating/tiling focus switch are not here: they route through the generic
// navigation handlers (navigation.cpp), which reach the scroll engine via
// ScreenModeRouter. Nor is the mode-neutral Retile: Daemon::handleRetile
// (navigation.cpp) picks the scrolling arm by the router's mode verdict and
// reaches the engine directly, and its wire lives in autotile_init.cpp
// beside the other Retile arm's handles.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"
#include "helpers.h"

#include "daemon/controllers/shortcutmanager.h"
#include "dbus/scrollingadaptor/scrollingadaptor.h"
#include "core/resolve/screenmoderouter.h"

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorZones/AssignmentEntry.h>

#include <functional>
#include <utility>

namespace PlasmaZones {

namespace {

/// Resolve the screen a scrolling shortcut should act on and the engine, or
/// nullptr when the focused screen is not in scrolling mode (the shortcut is
/// then a quiet no-op, matching the master-op handlers' behaviour off
/// autotile).
PhosphorScrollEngine::ScrollEngine* scrollTargetFor(PhosphorScreens::ScreenManager* screenManager,
                                                    WindowTrackingAdaptor* wta, ScreenModeRouter* router,
                                                    PhosphorEngine::PlacementEngineBase* engineBase,
                                                    QString* outScreenId)
{
    if (!router || !engineBase) {
        return nullptr;
    }
    const QString screenId = resolveShortcutScreenId(screenManager, wta);
    if (screenId.isEmpty() || !router->isScrollingMode(screenId)) {
        return nullptr;
    }
    auto* scroll = qobject_cast<PhosphorScrollEngine::ScrollEngine*>(engineBase);
    if (scroll && outScreenId) {
        // The out-param feeds the caller's context-disable gate; writing it
        // is not an engine-state mutation — the active-screen hint is
        // pushed by the caller only AFTER that gate passes, so a refused
        // shortcut leaves the engine untouched.
        *outScreenId = screenId;
    }
    return scroll;
}

} // namespace

void Daemon::connectScrollingShortcuts()
{
    if (!m_shortcutManager) {
        return;
    }
    // Tracked handles, mirroring initializeAutotile: a re-entry must not
    // stack duplicate lambda connections. The re-entry is PROSPECTIVE — this
    // has exactly one call site today (Daemon::start's wiring pass) — but
    // the mirrored shape keeps a future re-wire from silently double-firing
    // every verb.
    for (const QMetaObject::Connection& c : std::as_const(m_scrollingShortcutConnections)) {
        disconnect(c);
    }
    m_scrollingShortcutConnections.clear();
    // One resolver shared by every verb below: focused-screen + mode gate +
    // context-disable gate, then the concrete engine.
    const auto engineFor = [this](QString* outScreenId) -> PhosphorScrollEngine::ScrollEngine* {
        QString screenId;
        auto* scroll = scrollTargetFor(m_screenManager.get(), m_windowTrackingAdaptor, m_screenModeRouter.get(),
                                       m_scrollEngine.get(), &screenId);
        if (!scroll) {
            return nullptr;
        }
        if (isFocusedContextGatedForMode(screenId, PhosphorZones::AssignmentEntry::Scrolling)) {
            return nullptr;
        }
        // Gate passed — only now touch engine state.
        scroll->setActiveScreenHint(screenId);
        // Every keyboard strip verb funnels through here, which is why the
        // fullscreen release sits at the GATE rather than on the verbs: a verb
        // added to the table below inherits it, and one that forgot it would be
        // invisible. The wheel chord does the same thing for itself inside the
        // effect (TilingHandler::leaveNativeFullscreenTiles); this is the same
        // release for the half of the vocabulary that never reaches it.
        //
        // A scroll-tracked tile holding its OWN fullscreen refuses every
        // geometry commit through the effect's fullscreen bail, while the engine
        // below goes on scrolling and PARKING its column. The model and the
        // screen then disagree for the whole hold.
        //
        // BEFORE the verb, so the exit is already in flight when the relayout is
        // built. Fire-and-forget by construction — it is a signal, and the
        // compositor is the only party that can answer "is anything fullscreen
        // here", so the daemon does not ask and does not wait.
        //
        // Unconditional for a screen that passed the gate. Conditioning it would
        // mean the daemon keeping its own copy of per-window fullscreen state
        // purely to suppress a signal whose receiver already no-ops when there
        // is nothing to release.
        if (m_scrollingAdaptor) {
            Q_EMIT m_scrollingAdaptor->leaveNativeFullscreenRequested(screenId);
        }
        if (outScreenId) {
            *outScreenId = screenId;
        }
        return scroll;
    };

    // Every verb below is the same three steps — resolve, gate, call — so the
    // resolve+gate half is written once per SIGNAL ARITY and each connect
    // carries only the engine call. With the boilerplate gone, the list reads
    // as the verb table it is, and a verb missing from it is visible.
    using Scroll = PhosphorScrollEngine::ScrollEngine;
    const auto plainVerb = [engineFor](std::function<void(Scroll*, const QString&)> verb) {
        return [engineFor, verb = std::move(verb)]() {
            QString screenId;
            if (auto* scroll = engineFor(&screenId)) {
                verb(scroll, screenId);
            }
        };
    };
    const auto boolVerb = [engineFor](std::function<void(Scroll*, const QString&, bool)> verb) {
        return [engineFor, verb = std::move(verb)](bool flag) {
            QString screenId;
            if (auto* scroll = engineFor(&screenId)) {
                verb(scroll, screenId, flag);
            }
        };
    };
    const auto intVerb = [engineFor](std::function<void(Scroll*, const QString&, int)> verb) {
        return [engineFor, verb = std::move(verb)](int value) {
            QString screenId;
            if (auto* scroll = engineFor(&screenId)) {
                verb(scroll, screenId, value);
            }
        };
    };
    const auto wire = [this](auto signal, auto handler) {
        m_scrollingShortcutConnections << connect(m_shortcutManager.get(), signal, this, std::move(handler));
    };

    wire(&ShortcutManager::scrollFocusColumnEndRequested, boolVerb([](Scroll* s, const QString& id, bool last) {
        last ? s->focusColumnLast(id) : s->focusColumnFirst(id);
    }));
    wire(&ShortcutManager::scrollMoveColumnToEndRequested, boolVerb([](Scroll* s, const QString& id, bool last) {
        last ? s->moveColumnToLast(id) : s->moveColumnToFirst(id);
    }));
    wire(&ShortcutManager::scrollConsumeWindowRequested, plainVerb([](Scroll* s, const QString& id) {
        s->consumeWindowIntoColumn(id);
    }));
    wire(&ShortcutManager::scrollExpelWindowRequested, plainVerb([](Scroll* s, const QString& id) {
        s->expelWindowFromColumn(id);
    }));
    wire(&ShortcutManager::scrollConsumeOrExpelRequested, intVerb([](Scroll* s, const QString& id, int delta) {
        s->consumeOrExpelWindow(delta, id);
    }));
    wire(&ShortcutManager::scrollCenterColumnRequested, plainVerb([](Scroll* s, const QString& id) {
        s->centerColumn(id);
    }));
    wire(&ShortcutManager::scrollToggleColumnTabbedRequested, plainVerb([](Scroll* s, const QString& id) {
        s->toggleColumnTabbed(id);
    }));
    wire(&ShortcutManager::scrollToggleWindowedFullscreenRequested, plainVerb([](Scroll* s, const QString& id) {
        s->toggleWindowedFullscreen(id);
    }));
    wire(&ShortcutManager::scrollCycleColumnWidthRequested, intVerb([](Scroll* s, const QString& id, int delta) {
        s->cycleColumnPresetWidth(delta, id);
    }));
    wire(&ShortcutManager::scrollAdjustColumnWidthRequested, intVerb([](Scroll* s, const QString& id, int percent) {
        s->adjustColumnWidth(percent, id);
    }));
    wire(&ShortcutManager::scrollMaximizeColumnRequested, plainVerb([](Scroll* s, const QString& id) {
        s->toggleMaximizeColumn(id);
    }));
    wire(&ShortcutManager::scrollMaximizeToEdgesRequested, plainVerb([](Scroll* s, const QString& id) {
        s->toggleMaximizeToEdges(id);
    }));
    wire(&ShortcutManager::scrollExpandColumnRequested, plainVerb([](Scroll* s, const QString& id) {
        s->expandColumnToAvailableWidth(id);
    }));
    wire(&ShortcutManager::scrollCycleWindowHeightRequested, intVerb([](Scroll* s, const QString& id, int delta) {
        s->cycleWindowPresetHeight(delta, id);
    }));
    wire(&ShortcutManager::scrollAdjustWindowHeightRequested, intVerb([](Scroll* s, const QString& id, int percent) {
        s->adjustWindowHeight(percent, id);
    }));
    wire(&ShortcutManager::scrollMaximizeWindowHeightRequested, plainVerb([](Scroll* s, const QString& id) {
        s->maximizeWindowHeight(id);
    }));
    wire(&ShortcutManager::scrollExpandWindowRequested, plainVerb([](Scroll* s, const QString& id) {
        s->expandWindowToAvailableHeight(id);
    }));
    wire(&ShortcutManager::scrollEqualizeWindowHeightsRequested, plainVerb([](Scroll* s, const QString& id) {
        s->equalizeWindowHeights(id);
    }));
    wire(&ShortcutManager::scrollMinimizeWindowHeightRequested, plainVerb([](Scroll* s, const QString& id) {
        s->minimizeWindowHeight(id);
    }));
    wire(&ShortcutManager::scrollCenterVisibleColumnsRequested, plainVerb([](Scroll* s, const QString& id) {
        s->centerVisibleColumns(id);
    }));
    // POLARITY CONTRACT: the emitter passes bottom=false for
    // kIdScrollFocusWindowTop and true for the Bottom id (shortcutmanager.cpp
    // table rows); focusTileAtEnd(false) seeks the TOP tile. Swapping this
    // ternary compiles clean and no test drives the wire, so keep the
    // polarity next to the branch.
    wire(&ShortcutManager::scrollFocusWindowEndRequested, boolVerb([](Scroll* s, const QString& id, bool bottom) {
        bottom ? s->focusWindowBottom(id) : s->focusWindowTop(id);
    }));
    wire(&ShortcutManager::scrollFocusColumnPlainRequested, intVerb([](Scroll* s, const QString& id, int delta) {
        s->focusColumnPlain(delta, id);
    }));
    wire(&ShortcutManager::scrollFocusColumnWrapRequested, intVerb([](Scroll* s, const QString& id, int delta) {
        s->focusColumnWrap(delta, id);
    }));
    // POLARITY CONTRACT: the emitter passes floating=true for
    // kIdScrollMoveToFloating and false for the MoveToTiling id — same
    // swap-silently hazard as the focus-end wire above.
    wire(&ShortcutManager::scrollMoveToFloatRequested, boolVerb([](Scroll* s, const QString& id, bool floating) {
        floating ? s->moveFocusedToFloating(id) : s->moveFocusedToTiling(id);
    }));
    wire(&ShortcutManager::scrollViewRequested, intVerb([](Scroll* s, const QString& id, int percent) {
        s->scrollViewByPercent(percent, id);
    }));
    wire(&ShortcutManager::scrollEqualizeColumnWidthsRequested, plainVerb([](Scroll* s, const QString& id) {
        s->equalizeVisibleColumnWidths(id);
    }));
    wire(&ShortcutManager::scrollMinimizeColumnWidthRequested, plainVerb([](Scroll* s, const QString& id) {
        s->minimizeColumnWidth(id);
    }));
}

} // namespace PlasmaZones
