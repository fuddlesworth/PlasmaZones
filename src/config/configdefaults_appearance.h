// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// NOTE ON THE INCLUDE SET: this is the FIRST link of the ConfigDefaults
// inheritance chain, and the links below it (gaps → limits → shaders →
// screens → scrolling → configdefaults.h) include only their parent — so
// this header deliberately hosts the Qt and project includes the WHOLE chain
// consumes, not just what its own 300 lines read. Trimming an include that
// looks unused here breaks a downstream link.
#include "core/types/constants.h"
#include "core/types/enums.h"
#include "configkeys_scrolling.h"
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
// Zone-overlay colour/opacity/border defaults this header's own accessors
// delegate to. Explicit rather than transitive (the chain also reaches it
// through AssignmentEntry.h) — same policy as configdefaults_scrolling.h.
#include <PhosphorZones/ZoneDefaults.h>

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

namespace PhosphorAnimation {
class CurveRegistry;
}

namespace PlasmaZones {

// Chain link 1 of the ConfigDefaults inheritance chain (see configdefaults.h).
// Zone-overlay + window-decoration appearance default accessors. Inherited by
// ConfigDefaultsGaps and ultimately ConfigDefaults, so every ConfigDefaults::foo()
// call site resolves these static members through inheritance.
class ConfigDefaultsAppearance : public ConfigKeysScrolling
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Overlay (Snapping.Zones.*) Settings
    // ═══════════════════════════════════════════════════════════════════════════

    // The four zone-colour CONFIG keys are theme-fallback strings whose
    // schema default is the empty sentinel ("follow the system palette",
    // see themeFallbackColorDefault below). These QColor constants are NOT
    // those defaults — the *Fallback* names say so: they are the resolution
    // fallbacks Settings::resolvedSystemColor serves when no GUI application
    // (and therefore no palette) exists, and the shipped constants tests and
    // headless consumers compare against. Retuning one moves only that
    // no-palette fallback, never the shipped default.
    static QColor highlightFallbackColor()
    {
        return ::PhosphorZones::ZoneDefaults::HighlightColor;
    }
    static QColor inactiveFallbackColor()
    {
        return ::PhosphorZones::ZoneDefaults::InactiveColor;
    }
    static QColor borderFallbackColor()
    {
        return ::PhosphorZones::ZoneDefaults::BorderColor;
    }
    static QColor labelFontFallbackColor()
    {
        return ::PhosphorZones::ZoneDefaults::LabelFontColor;
    }
    // The stored default for the FOUR zone colour keys: the empty
    // follow-the-system sentinel. Routed through an accessor (not an inline
    // QString() at each schema site) per the ConfigDefaults-for-all-defaults
    // rule. The other theme-fallback keys (the Windows border/tint trio
    // below and the scrolling five) reach the same empty sentinel through
    // their own domain accessors, which double as their ISettings and stub
    // defaults — one value, two accessor families.
    static QString themeFallbackColorDefault()
    {
        return QString();
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
    // plugin never drift. Border colours default to the EMPTY theme-fallback
    // sentinel (resolved by the daemon's D-Bus getter against the zone
    // highlight / inactive colours); the border/title-bar scope defaults to
    // "tiled" (apply only to tiled/snapped windows).
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
    // Theme-fallback keys, like every other follow-the-system colour: EMPTY
    // means "follow the system accent" (the zone highlight / inactive colour),
    // resolved by the DAEMON before the value crosses D-Bus, so the effect's
    // empty-reply skew guard keeps meaning skew and only ever sees concrete
    // colours from config. The rules vocabulary is separate: rule actions
    // still carry PhosphorRules::BorderColorToken::Accent, because a rule
    // param's empty slot already means "unset". The inactive accessor
    // forwards to the active one, but only the STORED sentinel mirrors: the
    // two resolve against different targets (active → zone highlight,
    // inactive → zone inactive), same as the pre-v6 accent token did.
    static QString windowBorderColorActive()
    {
        return QString();
    }
    static QString windowBorderColorInactive()
    {
        return windowBorderColorActive();
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
    // defaults to the empty follow-the-system sentinel like the border
    // colours (resolved against the zone highlight).
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
