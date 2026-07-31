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

    m_scrollingShortcutConnections << connect(
        m_shortcutManager.get(), &ShortcutManager::scrollFocusColumnEndRequested, this, [engineFor](bool last) {
            QString screenId;
            if (auto* scroll = engineFor(&screenId)) {
                last ? scroll->focusColumnLast(screenId) : scroll->focusColumnFirst(screenId);
            }
        });
    m_scrollingShortcutConnections << connect(
        m_shortcutManager.get(), &ShortcutManager::scrollMoveColumnToEndRequested, this, [engineFor](bool last) {
            QString screenId;
            if (auto* scroll = engineFor(&screenId)) {
                last ? scroll->moveColumnToLast(screenId) : scroll->moveColumnToFirst(screenId);
            }
        });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::scrollConsumeWindowRequested,
                                              this, [engineFor]() {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->consumeWindowIntoColumn(screenId);
                                                  }
                                              });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::scrollExpelWindowRequested,
                                              this, [engineFor]() {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->expelWindowFromColumn(screenId);
                                                  }
                                              });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::scrollConsumeOrExpelRequested,
                                              this, [engineFor](int delta) {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->consumeOrExpelWindow(delta, screenId);
                                                  }
                                              });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::scrollCenterColumnRequested,
                                              this, [engineFor]() {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->centerColumn(screenId);
                                                  }
                                              });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(),
                                              &ShortcutManager::scrollToggleColumnTabbedRequested, this, [engineFor]() {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->toggleColumnTabbed(screenId);
                                                  }
                                              });
    m_scrollingShortcutConnections << connect(
        m_shortcutManager.get(), &ShortcutManager::scrollCycleColumnWidthRequested, this, [engineFor](int delta) {
            QString screenId;
            if (auto* scroll = engineFor(&screenId)) {
                scroll->cycleColumnPresetWidth(delta, screenId);
            }
        });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(),
                                              &ShortcutManager::scrollAdjustColumnWidthRequested, this,
                                              [engineFor](int deltaPercent) {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->adjustColumnWidth(deltaPercent, screenId);
                                                  }
                                              });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::scrollMaximizeColumnRequested,
                                              this, [engineFor]() {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->toggleMaximizeColumn(screenId);
                                                  }
                                              });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::scrollExpandColumnRequested,
                                              this, [engineFor]() {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->expandColumnToAvailableWidth(screenId);
                                                  }
                                              });
    m_scrollingShortcutConnections << connect(
        m_shortcutManager.get(), &ShortcutManager::scrollCycleWindowHeightRequested, this, [engineFor](int delta) {
            QString screenId;
            if (auto* scroll = engineFor(&screenId)) {
                scroll->cycleWindowPresetHeight(delta, screenId);
            }
        });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(),
                                              &ShortcutManager::scrollAdjustWindowHeightRequested, this,
                                              [engineFor](int deltaPercent) {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->adjustWindowHeight(deltaPercent, screenId);
                                                  }
                                              });
    m_scrollingShortcutConnections << connect(m_shortcutManager.get(),
                                              &ShortcutManager::scrollResetWindowHeightsRequested, this, [engineFor]() {
                                                  QString screenId;
                                                  if (auto* scroll = engineFor(&screenId)) {
                                                      scroll->resetWindowHeights(screenId);
                                                  }
                                              });
}

} // namespace PlasmaZones
