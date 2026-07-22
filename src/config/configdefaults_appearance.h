// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QRectF>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QtCore/qnamespace.h>

#include "core/types/constants.h"
#include "core/types/enums.h"
#include "configkeys.h"
#include "plasmazones_export.h"
// PhosphorTiles::AutotileDefaults lives in PhosphorTiles — config layer delegates to it for
// the user-facing default accessors.
#include <PhosphorTiles/AutotileConstants.h>
// Animation duration / stagger UI bounds — generic policy, not autotile-specific.
#include <PhosphorAnimation/AnimationLimits.h>
// Window decoration (border + title bar) defaults — shared across the D-Bus
// boundary with the compositor plugin so the daemon persists the same values
// the effect renders with before the async settings load lands.
#include <PhosphorCompositor/DecorationDefaults.h>
// Surface-shader decoration tree — the user-applied pack stack. The `border`
// PACK (data/surface/border) lives in that stack like any other pack; the
// window-manager border/title-bar APPEARANCE defaults live in the
// config-backed window-appearance settings above, not in this tree's default.
#include <PhosphorSurface/DecorationProfileTree.h>

namespace PhosphorAnimation {
class CurveRegistry;
}

namespace PlasmaZones {

// Chain link 1 of the ConfigDefaults inheritance chain (see configdefaults.h).
// Zone-overlay + window-decoration appearance default accessors. Inherited by
// ConfigDefaultsGaps and ultimately ConfigDefaults, so every ConfigDefaults::foo()
// call site resolves these static members through inheritance.
class ConfigDefaultsAppearance : public ConfigKeys
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Overlay (Snapping.Zones.*) Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool useSystemColors()
    {
        return true;
    }
    static QColor highlightColor()
    {
        return ::PhosphorZones::ZoneDefaults::HighlightColor;
    }
    static QColor inactiveColor()
    {
        return ::PhosphorZones::ZoneDefaults::InactiveColor;
    }
    static QColor borderColor()
    {
        return ::PhosphorZones::ZoneDefaults::BorderColor;
    }
    static QColor labelFontColor()
    {
        return ::PhosphorZones::ZoneDefaults::LabelFontColor;
    }
    static double activeOpacity()
    {
        return ::PhosphorZones::ZoneDefaults::Opacity;
    }
    static constexpr qreal activeOpacityMin()
    {
        return 0.0;
    }
    static constexpr qreal activeOpacityMax()
    {
        return 1.0;
    }
    static double inactiveOpacity()
    {
        return ::PhosphorZones::ZoneDefaults::InactiveOpacity;
    }
    static constexpr qreal inactiveOpacityMin()
    {
        return 0.0;
    }
    static constexpr qreal inactiveOpacityMax()
    {
        return 1.0;
    }
    static int borderWidth()
    {
        return ::PhosphorZones::ZoneDefaults::BorderWidth;
    }
    static constexpr int borderWidthMin()
    {
        return 0;
    }
    static constexpr int borderWidthMax()
    {
        return 10;
    }
    static int borderRadius()
    {
        return ::PhosphorZones::ZoneDefaults::BorderRadius;
    }
    static constexpr int borderRadiusMin()
    {
        return 0;
    }
    static constexpr int borderRadiusMax()
    {
        return 50;
    }
    static QString labelFontFamily()
    {
        return QString();
    }
    static double labelFontSizeScale()
    {
        return 1.0;
    }
    static constexpr qreal labelFontSizeScaleMin()
    {
        return 0.25;
    }
    static constexpr qreal labelFontSizeScaleMax()
    {
        return 3.0;
    }
    static int labelFontWeight()
    {
        return 700;
    }
    static constexpr int labelFontWeightMin()
    {
        return 100;
    }
    static constexpr int labelFontWeightMax()
    {
        return 900;
    }
    static bool labelFontItalic()
    {
        return false;
    }
    static bool labelFontUnderline()
    {
        return false;
    }
    static bool labelFontStrikeout()
    {
        return false;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Window Decoration Appearance (Windows.*) Settings
    //
    // Tiled/snapped window border + title bar defaults. Distinct from the
    // zone-overlay border constants above: these come from the shared
    // PhosphorCompositor::DecorationDefaults so the daemon and the compositor
    // plugin never drift. Border colours default to the "accent" sentinel
    // (resolved to the system accent colour at render time); the border/title-bar
    // scope defaults to "tiled" (apply only to tiled/snapped windows).
    // ═══════════════════════════════════════════════════════════════════════════

    static bool showWindowBorder()
    {
        return ::PhosphorCompositor::DecorationDefaults::ShowBorder;
    }
    static int windowBorderWidth()
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidth;
    }
    static constexpr int windowBorderWidthMin()
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidthMin;
    }
    static constexpr int windowBorderWidthMax()
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidthMax;
    }
    static int windowBorderRadius()
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadius;
    }
    static constexpr int windowBorderRadiusMin()
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadiusMin;
    }
    static constexpr int windowBorderRadiusMax()
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadiusMax;
    }
    // Decoration focus cross-fade (uSurfaceFocused ramp) duration, ms. A
    // standalone decoration setting, independent of the window animation
    // system; 0 switches instantly.
    static constexpr int focusFadeDuration()
    {
        return ::PhosphorCompositor::DecorationDefaults::FocusFadeMs;
    }
    static constexpr int focusFadeDurationMin()
    {
        return ::PhosphorCompositor::DecorationDefaults::FocusFadeMsMin;
    }
    static constexpr int focusFadeDurationMax()
    {
        return ::PhosphorCompositor::DecorationDefaults::FocusFadeMsMax;
    }
    static bool hideWindowTitleBars()
    {
        return ::PhosphorCompositor::DecorationDefaults::HideTitleBars;
    }
    // The "accent" sentinel (PhosphorRules::BorderColorToken::Accent) — the effect
    // resolves it to the live system accent / inactive colour at paint time. Kept
    // as a bare literal here rather than pulling PhosphorRules into this
    // widely-included header; the inactive default mirrors the active one.
    static QString windowBorderColorActive()
    {
        return QStringLiteral("accent");
    }
    static QString windowBorderColorInactive()
    {
        return windowBorderColorActive();
    }
    // Concrete opaque colour the settings app seeds into config when the user
    // leaves "follow the system accent" mode (KDE accent blue, #AARRGGBB). Lives
    // here so the settings page's fallback stays single-sourced with the config
    // layer rather than hardcoded in QML.
    static QString windowBorderColorAccentFallbackHex()
    {
        return QStringLiteral("#FF3DAEE9");
    }
    // Fresh-install "Apply to" scope for both the border and the title bar. The
    // token set lives in PhosphorCompositor::WindowAppearanceScope (shared with the
    // schema validator and the effect).
    static QString windowBorderScope()
    {
        return QString(::PhosphorCompositor::WindowAppearanceScope::Tiled);
    }
    static QString windowTitleBarScope()
    {
        return windowBorderScope();
    }

    // ── Plain opacity+tint layer (Windows.* ShowOpacityTint/Opacity/Tint*) ──
    // The opacity analogue of the plain border: config-backed, rendered by the
    // built-in "opacity-tint" surface pack in easy mode (no user decoration
    // packs), suppressed wholesale by any user pack. Defaults mirror the
    // pack's own parameter defaults (full opacity, no tint) so enabling the
    // toggle changes nothing until the user moves a slider; the tint colour
    // defaults to the accent sentinel like the border colours.
    static bool showWindowOpacityTint()
    {
        return false;
    }
    static double windowOpacity()
    {
        return 1.0;
    }
    static constexpr qreal windowOpacityMin()
    {
        return 0.0;
    }
    static constexpr qreal windowOpacityMax()
    {
        return 1.0;
    }
    static double windowTintStrength()
    {
        return 0.0;
    }
    static constexpr qreal windowTintStrengthMin()
    {
        return 0.0;
    }
    static constexpr qreal windowTintStrengthMax()
    {
        return 1.0;
    }
    static QString windowTintColor()
    {
        return windowBorderColorActive();
    }
    static QString windowOpacityTintScope()
    {
        return windowBorderScope();
    }
};

} // namespace PlasmaZones
