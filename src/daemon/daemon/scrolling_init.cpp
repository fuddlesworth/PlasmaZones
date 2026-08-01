// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — scrolling-mode shortcut wiring
//
// Connects the ShortcutManager's scroll-specific column signals to the
// concrete ScrollEngine. Directional focus/move/swap and float shortcuts are
// NOT here — they route through the generic navigation handlers
// (navigation.cpp), which reach the scroll engine via ScreenModeRouter.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"
#include "helpers.h"

#include "daemon/controllers/shortcutmanager.h"
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
    // stack duplicate lambda connections.
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
    wire(&ShortcutManager::scrollCycleColumnWidthRequested, intVerb([](Scroll* s, const QString& id, int delta) {
        s->cycleColumnPresetWidth(delta, id);
    }));
    wire(&ShortcutManager::scrollAdjustColumnWidthRequested, intVerb([](Scroll* s, const QString& id, int percent) {
        s->adjustColumnWidth(percent, id);
    }));
    wire(&ShortcutManager::scrollMaximizeColumnRequested, plainVerb([](Scroll* s, const QString& id) {
        s->toggleMaximizeColumn(id);
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
    wire(&ShortcutManager::scrollResetWindowHeightsRequested, plainVerb([](Scroll* s, const QString& id) {
        s->resetWindowHeights(id);
    }));
}

} // namespace PlasmaZones
