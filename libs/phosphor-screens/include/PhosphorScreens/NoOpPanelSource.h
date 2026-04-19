// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "IPanelSource.h"
#include "phosphorscreens_export.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace Phosphor::Screens {

/**
 * @brief Trivial @ref IPanelSource that reports zero offsets and ready=true.
 *
 * Use on hosts where panels don't reserve geometry (Wayfire, Sway, COSMIC
 * with manual layout) or in unit tests. ScreenManager treats `ready()=true`
 * as "no point waiting on panel geometry, the screen rect is the rect" so
 * `panelGeometryReady` fires on the first `start()` and downstream layout
 * starts immediately instead of stalling.
 *
 * Header-only and inline — no compiled object code.
 */
class PHOSPHORSCREENS_EXPORT NoOpPanelSource final : public IPanelSource
{
    Q_OBJECT
public:
    explicit NoOpPanelSource(QObject* parent = nullptr)
        : IPanelSource(parent)
    {
    }

    void start() override
    {
        // Synthetic per-screen fan-out mirrors PlasmaPanelSource's first-ready
        // behaviour so consumers wired only to panelOffsetsChanged (rather
        // than ScreenManager::panelGeometryReady) still get a recompute kick
        // on non-KDE hosts. Zero offsets are reported via currentOffsets().
        if (qGuiApp) {
            const auto screens = QGuiApplication::screens();
            for (auto* s : screens) {
                Q_EMIT panelOffsetsChanged(s);
            }
        }
        Q_EMIT requeryCompleted();
    }
    void stop() override
    {
    }
    Offsets currentOffsets(QScreen*) const override
    {
        return {};
    }
    bool ready() const override
    {
        return true;
    }
    void requestRequery(int delayMs = 0) override
    {
        // Honour the caller's delay so "wait for panel editor to settle"
        // timing is consistent across IPanelSource implementations —
        // PlasmaPanelSource's timer-path delays its completion signal, and
        // callers that pass a positive delayMs expect that behaviour
        // regardless of host desktop. 0/negative stays synchronous
        // (matches the IPanelSource::requestRequery contract).
        if (delayMs <= 0) {
            Q_EMIT requeryCompleted();
            return;
        }
        QTimer::singleShot(delayMs, this, [this]() {
            Q_EMIT requeryCompleted();
        });
    }
};

} // namespace Phosphor::Screens
