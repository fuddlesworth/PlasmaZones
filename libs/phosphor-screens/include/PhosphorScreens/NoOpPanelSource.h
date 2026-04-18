// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "IPanelSource.h"
#include "phosphorscreens_export.h"

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
    void requestRequery(int /*delayMs*/ = 0) override
    {
        Q_EMIT requeryCompleted();
    }
};

} // namespace Phosphor::Screens
