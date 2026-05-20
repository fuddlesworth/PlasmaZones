// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// StubScrollSettings — narrow stub for IScrollSettings (PZ-side).
//
// Implements only the scroll master gate and column-decoration surface —
// the 17 virtuals of PlasmaZones::IScrollSettings. Adding a new scroll-only
// setting requires touching IScrollSettings and this stub, nothing else;
// the unified StubSettings inherits this helper so existing tests are
// unchanged.
//
// Standalone-usable: a test that needs an IScrollSettings* but no other
// setting surface can construct StubScrollSettings on its own without
// pulling the full ISettings abstract-base requirement. See StubSettings.h
// for the unified composition.
//
// Note: this is the *PZ-side* IScrollSettings (master gate + decoration).
// The engine-side PhosphorEngine::IScrollSettings (geometry — gaps, default
// column width, presets) is a different contract and lives separately.

#include "config/configdefaults.h"
#include "core/settings_interfaces.h"

namespace PlasmaZones {

class StubScrollSettings : public virtual IScrollSettings
{
public:
    ~StubScrollSettings() override = default;

    bool scrollingEnabled() const override
    {
        return ConfigDefaults::scrollingEnabled();
    }
    void setScrollingEnabled(bool) override
    {
    }
    bool scrollShowBorder() const override
    {
        return ConfigDefaults::scrollShowBorder();
    }
    void setScrollShowBorder(bool) override
    {
    }
    int scrollBorderWidth() const override
    {
        return ConfigDefaults::scrollBorderWidth();
    }
    void setScrollBorderWidth(int) override
    {
    }
    int scrollBorderRadius() const override
    {
        return ConfigDefaults::scrollBorderRadius();
    }
    void setScrollBorderRadius(int) override
    {
    }
    QColor scrollBorderColor() const override
    {
        return ConfigDefaults::scrollBorderColor();
    }
    void setScrollBorderColor(const QColor&) override
    {
    }
    QColor scrollInactiveBorderColor() const override
    {
        return ConfigDefaults::scrollInactiveBorderColor();
    }
    void setScrollInactiveBorderColor(const QColor&) override
    {
    }
    bool scrollUseSystemBorderColors() const override
    {
        return ConfigDefaults::scrollUseSystemBorderColors();
    }
    void setScrollUseSystemBorderColors(bool) override
    {
    }
    bool scrollHideTitleBars() const override
    {
        return ConfigDefaults::scrollHideTitleBars();
    }
    void setScrollHideTitleBars(bool) override
    {
    }
    bool scrollFocusNewWindows() const override
    {
        return ConfigDefaults::scrollFocusNewWindows();
    }
    void setScrollFocusNewWindows(bool) override
    {
    }
    bool scrollFocusFollowsMouse() const override
    {
        return ConfigDefaults::scrollFocusFollowsMouse();
    }
    void setScrollFocusFollowsMouse(bool) override
    {
    }
};

} // namespace PlasmaZones
