// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <memory>

#include "autotile/AutotileEngine.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingState.h>

namespace PlasmaZones::TestHelpers {

/// Test-process AlgorithmRegistry (shared).
///
/// AlgorithmRegistry::instance() was removed when PlasmaZones moved to
/// per-process registry ownership for plugin-friendly architecture.
/// Most tests just need *some* registry across factory calls so engines
/// built in different test cases see the same built-in catalog without
/// paying the cost of re-registering the (sizeable) built-ins per test
/// — this Meyer-style local serves that purpose. Production code
/// injects its own registry per composition root and never uses this.
///
/// @warning State is shared across ALL tests in a single test-binary
/// run. Tests that call @c registerAlgorithm / @c unregisterAlgorithm
/// leak that mutation into subsequent tests. If your test mutates the
/// registry (registers custom algorithms, changes preview params),
/// prefer @ref scopedRegistry to get a fresh instance that is torn
/// down when the unique_ptr leaves scope.
inline PhosphorTiles::AlgorithmRegistry* testRegistry()
{
    static PhosphorTiles::AlgorithmRegistry s_registry;
    return &s_registry;
}

/// Per-test AlgorithmRegistry.
///
/// Use this when your test mutates the registry (register/unregister
/// custom algorithms, set preview params) — the returned instance is
/// owned by the caller and torn down at scope end, so mutations do NOT
/// leak into subsequent tests. Carries the full built-in catalogue like
/// @ref testRegistry (registration runs automatically in the ctor).
///
/// Prefer @ref testRegistry for read-only tests to avoid the built-in
/// re-registration cost.
inline std::unique_ptr<PhosphorTiles::AlgorithmRegistry> scopedRegistry()
{
    return std::make_unique<PhosphorTiles::AlgorithmRegistry>();
}

/**
 * @brief Create an engine with a single autotile screen and N windows.
 *
 * Uses engine->windowOpened() + processEvents() to register windows through
 * the proper lifecycle (populating m_windowToScreen), rather than adding
 * directly to PhosphorTiles::TilingState which leaves internal maps empty.
 *
 * @param screen      Screen name for the autotile screen.
 * @param count       Number of windows to open (named "win1" .. "winN").
 * @param focusedWindow  Optional window ID to focus after all windows are opened.
 * @return Heap-allocated AutotileEngine (caller owns the pointer).
 */
inline AutotileEngine* createEngineWithWindows(const QString& screen, int count,
                                               const QString& focusedWindow = QString())
{
    auto* engine = new AutotileEngine(nullptr, nullptr, nullptr, testRegistry());
    engine->setAutotileScreens({screen});

    for (int i = 1; i <= count; ++i) {
        engine->windowOpened(QStringLiteral("win%1").arg(i), screen);
    }
    QCoreApplication::processEvents();

    if (!focusedWindow.isEmpty()) {
        PhosphorTiles::TilingState* state = engine->tilingStateForScreen(screen);
        state->setFocusedWindow(focusedWindow);
        engine->windowFocused(focusedWindow, screen);
    }

    return engine;
}

} // namespace PlasmaZones::TestHelpers
