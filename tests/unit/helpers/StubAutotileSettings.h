// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// StubAutotileSettings — narrow stub for IAutotileSettings (PZ-side).
//
// Implements only the autotile master gate, decoration surface, and
// drag-insert triggers — the 18 virtuals of PlasmaZones::IAutotileSettings.
// Adding a new autotile-only setting requires touching IAutotileSettings
// and this stub, nothing else; the unified StubSettings inherits this
// helper so existing tests are unchanged.
//
// Standalone-usable: a test that needs an IAutotileSettings* but no other
// setting surface can construct StubAutotileSettings on its own without
// pulling the full ISettings abstract-base requirement. See StubSettings.h
// for the unified composition.

#include "config/configdefaults.h"
#include "core/settings_interfaces.h"

namespace PlasmaZones {

class StubAutotileSettings : public virtual IAutotileSettings
{
public:
    ~StubAutotileSettings() override = default;

    bool autotileEnabled() const override
    {
        return ConfigDefaults::autotileEnabled();
    }
    void setAutotileEnabled(bool) override
    {
    }
    bool autotileFocusFollowsMouse() const override
    {
        return false;
    }
    void setAutotileFocusFollowsMouse(bool) override
    {
    }
    bool autotileHideTitleBars() const override
    {
        return false;
    }
    void setAutotileHideTitleBars(bool) override
    {
    }
    bool autotileShowBorder() const override
    {
        return false;
    }
    void setAutotileShowBorder(bool) override
    {
    }
    int autotileBorderWidth() const override
    {
        return 2;
    }
    void setAutotileBorderWidth(int) override
    {
    }
    int autotileBorderRadius() const override
    {
        return 0;
    }
    void setAutotileBorderRadius(int) override
    {
    }
    QColor autotileBorderColor() const override
    {
        return Qt::white;
    }
    void setAutotileBorderColor(const QColor&) override
    {
    }
    QColor autotileInactiveBorderColor() const override
    {
        return {};
    }
    void setAutotileInactiveBorderColor(const QColor&) override
    {
    }
    bool autotileUseSystemBorderColors() const override
    {
        return false;
    }
    void setAutotileUseSystemBorderColors(bool) override
    {
    }
    StickyWindowHandling autotileStickyWindowHandling() const override
    {
        return StickyWindowHandling::TreatAsNormal;
    }
    void setAutotileStickyWindowHandling(StickyWindowHandling) override
    {
    }
    AutotileDragBehavior autotileDragBehavior() const override
    {
        return AutotileDragBehavior::Float;
    }
    void setAutotileDragBehavior(AutotileDragBehavior) override
    {
    }
    AutotileOverflowBehavior autotileOverflowBehavior() const override
    {
        return AutotileOverflowBehavior::Float;
    }
    void setAutotileOverflowBehavior(AutotileOverflowBehavior) override
    {
    }
    QVariantList autotileDragInsertTriggers() const override
    {
        return ConfigDefaults::autotileDragInsertTriggers();
    }
    void setAutotileDragInsertTriggers(const QVariantList&) override
    {
    }
    bool autotileDragInsertToggle() const override
    {
        return false;
    }
    void setAutotileDragInsertToggle(bool) override
    {
    }
};

} // namespace PlasmaZones
