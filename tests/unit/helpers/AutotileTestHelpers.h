// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include "autotile/AutotileEngine.h"
#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingState.h"

namespace PlasmaZones::TestHelpers {

/**
 * @brief Create an engine with a single autotile screen and N windows.
 *
 * Uses engine->windowOpened() + processEvents() to register windows through
 * the proper lifecycle (populating m_windowToScreen), rather than adding
 * directly to TilingState which leaves internal maps empty.
 *
 * @param screen      Screen name for the autotile screen.
 * @param count       Number of windows to open (named "win1" .. "winN").
 * @param focusedWindow  Optional window ID to focus after all windows are opened.
 * @return Heap-allocated AutotileEngine (caller owns the pointer).
 */
inline AutotileEngine* createEngineWithWindows(const QString& screen, int count,
                                               const QString& focusedWindow = QString())
{
    auto* engine = new AutotileEngine(nullptr, nullptr, nullptr);
    engine->setAutotileScreens({screen});

    for (int i = 1; i <= count; ++i) {
        engine->windowOpened(QStringLiteral("win%1").arg(i), screen);
    }
    QCoreApplication::processEvents();

    if (!focusedWindow.isEmpty()) {
        TilingState* state = engine->stateForScreen(screen);
        state->setFocusedWindow(focusedWindow);
        engine->windowFocused(focusedWindow, screen);
    }

    return engine;
}

} // namespace PlasmaZones::TestHelpers
