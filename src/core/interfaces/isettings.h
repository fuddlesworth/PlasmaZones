// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// ISettings — PZ global settings facade.
//
// Split out of interfaces.h so consumers can include just the settings
// contract without pulling zone/layout/overlay interfaces. ISettings is
// explicitly PZ-owned and is NOT a candidate for the phosphor-zones
// extraction — zones that need a tuning value should take it directly
// (see PhosphorZones::ZoneDetector::setAdjacentThreshold for the pattern).

#include "plasmazones_export.h"
#include "core/types/enums.h"
#include "settings_interfaces.h"

// Explicit rather than transitive: the drop indicator's no-palette colour IS
// the zone overlay's highlight, so this header genuinely depends on that
// symbol.
#include <PhosphorZones/ZoneDefaults.h>

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

namespace PhosphorSurfaceShaders {
class DecorationProfileTree;
}

namespace PlasmaZones {

namespace isettings_detail {
/// The drop indicator's colour when nothing can resolve one: the shipped zone
/// highlight forced opaque. Shared by the two colour defaults below so the
/// value is written once. See scrollingDropIndicatorColor() for why opaque.
inline QColor opaqueDropIndicatorFallback()
{
    QColor color = ::PhosphorZones::ZoneDefaults::HighlightColor;
    color.setAlpha(255);
    return color;
}
} // namespace isettings_detail

/**
 * @brief Abstract interface for settings management
 *
 * Allows dependency inversion - components depend on this interface
 * rather than concrete Settings implementation. Inherits from focused
 * sub-interfaces so components can depend on just what they need.
 *
 * Note on the sub-interface NOTIFY surface: the sub-interfaces
 * (IZoneActivationSettings, IZoneSelectorSettings, etc.) are
 * deliberately non-QObject and so cannot declare Q_SIGNALS of their
 * own. All notify signals live on this ISettings level. The codebase
 * idiom is for a consumer that needs both the value AND the signal to
 * hold BOTH pointers — `IZoneSelectorSettings*` for reads/writes,
 * `ISettings*` (or `QObject*`) for `connect()`:
 *
 * @code
 * class Consumer {
 * public:
 *     Consumer(ISettings* settings)
 *         : m_settings(settings),               // for connect()
 *           m_selector(settings)                // for value access
 *     {
 *         connect(m_settings, &ISettings::zoneSelectorEnabledChanged,
 *                 this, &Consumer::onChanged);
 *     }
 * private:
 *     ISettings* m_settings;
 *     IZoneSelectorSettings* m_selector;
 * };
 * @endcode
 *
 * A `dynamic_cast<ISettings*>(zoneSelectorSettingsPtr)` also works at the
 * call site since `Settings` (the only concrete subclass) inherits from both
 * bases, but holding both pointers from construction is cheaper and clearer.
 */
class PLASMAZONES_EXPORT ISettings : public QObject,
                                     public IZoneActivationSettings,
                                     public IZoneVisualizationSettings,
                                     public IZoneGeometrySettings,
                                     public IWindowExclusionSettings,
                                     public IZoneSelectorSettings,
                                     public IScrollingZoneSelectorSettings,
                                     public IWindowBehaviorSettings,
                                     public IDefaultLayoutSettings,
                                     public IOrderingSettings,
                                     public IAnimationSettings
{
    Q_OBJECT

public:
    explicit ISettings(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~ISettings() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // All settings methods are inherited from the segregated interfaces:
    //   - IZoneActivationSettings: drag modifiers, activation triggers
    //   - IZoneVisualizationSettings: colors, opacity, shader effects
    //   - IZoneGeometrySettings: padding, gaps, thresholds, performance
    //   - IWindowExclusionSettings: transient windows, size filters
    //   - IZoneSelectorSettings: zone selector UI configuration
    //   - IScrollingZoneSelectorSettings: strip-mode drag selector UI configuration
    //   - IWindowBehaviorSettings: snap restore, sticky handling
    //   - IDefaultLayoutSettings: default layout ID
    //   - IOrderingSettings: manual layout / algorithm / scrolling-template ordering
    //   - IAnimationSettings: animation/shader-profile state + window filtering
    //
    // See settings_interfaces.h for the full API.
    // ═══════════════════════════════════════════════════════════════════════════

    // Animation + shader-profile settings are segregated into
    // IAnimationSettings (mixed in above). The matching NOTIFY signals stay on
    // this QObject (see Q_SIGNALS below): Qt forbids multiple QObject
    // inheritance, so a consumer that needs an animation signal depends on
    // ISettings, not IAnimationSettings alone.
    //
    // animationExcludedApplications / animationExcludedWindowClasses virtuals
    // retired in v4 — the lists folded into ExcludeAnimations Rules; the
    // KWin effect now derives its m_animationExclusionRuleSet from the unified
    // rule store via PhosphorRules::ExclusionRules::excludeAnimationsRulesFrom.

    // Autotile focus settings (fetched by KWin effect via D-Bus)
    virtual bool autotileFocusFollowsMouse() const = 0;
    virtual void setAutotileFocusFollowsMouse(bool enabled) = 0;
    // Snapping focus behavior. focusNewWindows is read daemon-side by SnapAdaptor
    // to activate auto-placed-on-open windows; focusFollowsMouse is fetched by the
    // KWin effect via D-Bus.
    virtual bool snappingFocusNewWindows() const = 0;
    virtual void setSnappingFocusNewWindows(bool enabled) = 0;
    virtual bool snappingFocusFollowsMouse() const = 0;
    virtual void setSnappingFocusFollowsMouse(bool enabled) = 0;

    virtual StickyWindowHandling autotileStickyWindowHandling() const = 0;
    virtual void setAutotileStickyWindowHandling(StickyWindowHandling handling) = 0;
    virtual AutotileDragBehavior autotileDragBehavior() const = 0;
    virtual void setAutotileDragBehavior(AutotileDragBehavior behavior) = 0;
    virtual AutotileOverflowBehavior autotileOverflowBehavior() const = 0;
    virtual void setAutotileOverflowBehavior(AutotileOverflowBehavior behavior) = 0;

    // Autotile drag-insert triggers: hold-to-activate list for live
    // re-inserting a dragged window into the autotile stack.
    virtual QVariantList autotileDragInsertTriggers() const = 0;
    virtual void setAutotileDragInsertTriggers(const QVariantList& triggers) = 0;
    virtual bool autotileDragInsertToggle() const = 0;
    virtual void setAutotileDragInsertToggle(bool enable) = 0;

    // Scrolling twins: hold-to-activate list for live re-inserting a dragged
    // window into the scroll strip (WindowDragAdaptor reads them per drag,
    // beside the autotile pair above).
    virtual QVariantList scrollingDragInsertTriggers() const = 0;
    virtual void setScrollingDragInsertTriggers(const QVariantList& triggers) = 0;
    virtual bool scrollingDragInsertToggle() const = 0;
    virtual void setScrollingDragInsertToggle(bool enable) = 0;

    // Per-algorithm autotile settings map. Settings inherits from
    // PhosphorEngine::IAutotileSettings (which also declares these),
    // so the override in Settings covers both bases — the redundant
    // declaration here is the price of letting page controllers
    // depend on ISettings without dragging in PhosphorTileEngine.
    virtual QVariantMap autotilePerAlgorithmSettings() const = 0;
    virtual void setAutotilePerAlgorithmSettings(const QVariantMap& settings) = 0;

    // Hierarchical per-surface decoration tree — a DecorationProfile (surface
    // shader-pack chain + its per-pack parameters) keyed on a dot-path surface
    // namespace. Mirrors the animation shaderProfileTree pair (which lives on
    // IAnimationSettings): the typed getter returns the parsed tree, and the
    // JSON-string facade routes through it for the Q_PROPERTY meta-object
    // dirty-tracking loop. Lives on the interface so page controllers and the
    // settings adaptor depend on ISettings, not the concrete Settings.
    virtual PhosphorSurfaceShaders::DecorationProfileTree decorationProfileTree() const = 0;
    virtual void setDecorationProfileTree(const PhosphorSurfaceShaders::DecorationProfileTree& tree) = 0;
    virtual QString decorationProfileTreeJson() const = 0;
    virtual void setDecorationProfileTreeJson(const QString& json) = 0;

    // Decorations.Performance — an animated pack repaints every window carrying
    // it on every vsync, and that alone keeps the GPU in its top performance
    // state regardless of how cheap the per-frame work is. The three gates below
    // bound WHEN the chain redraws; the blur-scale multiplier after them shrinks
    // the per-frame work instead.
    virtual bool decorationAnimateFocusedOnly() const = 0;
    virtual void setDecorationAnimateFocusedOnly(bool value) = 0;
    virtual bool decorationPauseWhenIdle() const = 0;
    virtual void setDecorationPauseWhenIdle(bool value) = 0;
    virtual int decorationIdleTimeoutSec() const = 0;
    virtual void setDecorationIdleTimeoutSec(int value) = 0;
    /// Multiplier on the bufferScale each decoration pack declares for its
    /// buffer passes (the blur pyramid density). Not a WHEN gate like its
    /// group-mates: it shrinks the per-frame work instead, which is the lever
    /// that matters on integrated GPUs where the blur passes themselves are
    /// the cost.
    virtual double decorationBlurScaleMultiplier() const = 0;
    virtual void setDecorationBlurScaleMultiplier(double value) = 0;

    /// The system colour scheme as a "light" / "dark" token, or empty when the
    /// process cannot observe a palette (no GUI application, or an off-GUI-thread
    /// caller). Pairs with the systemColorSchemeChanged signal below: the signal
    /// says "it flipped", this says "to what".
    ///
    /// On the interface rather than only as Settings' static, so the consumers
    /// that stamp the ColorScheme match field (the daemon's registry provider and
    /// the WindowTrackingAdaptor query builder) reach it through their INJECTED
    /// settings and a test double can substitute a fixed scheme. Settings
    /// implements it by delegating to its static derivation, which stays the one
    /// place the palette is classified.
    ///
    /// Defaulted rather than pure so an implementation with no palette to
    /// classify (a test double) inherits the honest "unknown" answer instead of
    /// being forced to fabricate one — empty is exactly what the real accessor
    /// returns in a process without a GUI application, and the rule resolvers
    /// already hold ColorScheme-negating rules out on an empty token. A double
    /// that wants to exercise a light/dark rule overrides this.
    virtual QString colorSchemeToken() const
    {
        return QString();
    }

    // Snapping behavior triggers (dragActivation, zoneSpan, snapAssist)
    // are declared by the IZoneActivationSettings / IZoneSelectorSettings
    // sub-interfaces ISettings inherits from — see settings_interfaces.h.
    // Re-declaring them here would shadow the parent virtual.

    // Rendering backend (pipeline-level, not specific to any sub-interface)
    virtual QString renderingBackend() const = 0;
    virtual void setRenderingBackend(const QString& backend) = 0;

    // GPU the daemon renders on: "auto" or a "vendor:device" hex PCI pair.
    // Applied at daemon/editor startup (before QGuiApplication), so like the
    // backend it takes effect on restart.
    virtual QString gpuDevice() const = 0;
    virtual void setGpuDevice(const QString& gpu) = 0;

    // Window decoration appearance (tiled/snapped window border + title bar).
    // Mode-neutral, distinct from the ZONE OVERLAY border settings
    // (borderWidth/borderRadius/borderColor on IZoneVisualizationSettings) —
    // these describe the actual window decoration the compositor paints, edited
    // on the Window Appearance settings page.
    virtual bool showWindowBorder() const = 0;
    virtual void setShowWindowBorder(bool show) = 0;
    virtual QString windowBorderScope() const = 0;
    virtual void setWindowBorderScope(const QString& scope) = 0;
    virtual int windowBorderWidth() const = 0;
    virtual void setWindowBorderWidth(int width) = 0;
    virtual int windowBorderRadius() const = 0;
    virtual void setWindowBorderRadius(int radius) = 0;
    // The two border colours are theme-fallback strings: an #AARRGGBB hex,
    // or EMPTY meaning "follow the system" (active → the zone highlight,
    // inactive → the zone inactive colour). An empty return is meaningful,
    // not "unset". The daemon's D-Bus getter resolves the sentinel before
    // the value crosses the wire, so the effect only ever sees concrete
    // colours; the rules vocabulary keeps its separate "accent" token.
    virtual QString windowBorderColorActive() const = 0;
    virtual void setWindowBorderColorActive(const QString& color) = 0;
    virtual QString windowBorderColorInactive() const = 0;
    virtual void setWindowBorderColorInactive(const QString& color) = 0;
    virtual bool hideWindowTitleBars() const = 0;
    virtual void setHideWindowTitleBars(bool hide) = 0;
    virtual QString windowTitleBarScope() const = 0;
    virtual void setWindowTitleBarScope(const QString& scope) = 0;
    // Decoration focus cross-fade duration (ms): how long uSurfaceFocused
    // ramps between focused and unfocused for the decoration packs that mix
    // by focus. Independent of the window animation system; 0 = instant.
    virtual int focusFadeDuration() const = 0;
    virtual void setFocusFadeDuration(int ms) = 0;
    // Plain opacity+tint layer (Windows.* ShowOpacityTint/Opacity/Tint*): the
    // opacity analogue of the plain border, rendered by the built-in
    // "opacity-tint" surface pack and suppressed by any user decoration pack.
    // Opacity and tint strength are [0.0, 1.0]; the tint colour carries the
    // same theme-fallback contract as the border colours above (#AARRGGBB
    // hex, or EMPTY meaning "follow the zone highlight", resolved by the
    // daemon before D-Bus).
    virtual bool showWindowOpacityTint() const = 0;
    virtual void setShowWindowOpacityTint(bool show) = 0;
    virtual QString windowOpacityTintScope() const = 0;
    virtual void setWindowOpacityTintScope(const QString& scope) = 0;
    virtual double windowOpacity() const = 0;
    virtual void setWindowOpacity(double opacity) = 0;
    virtual double windowTintStrength() const = 0;
    virtual void setWindowTintStrength(double strength) = 0;
    virtual QString windowTintColor() const = 0;
    virtual void setWindowTintColor(const QString& color) = 0;

    // Editor settings — used by EditorPageController. Editor-scope rather
    // than Snapping/Tiling-scope, so they don't fit any sub-interface.
    virtual QString editorDuplicateShortcut() const = 0;
    virtual void setEditorDuplicateShortcut(const QString& shortcut) = 0;
    virtual QString editorSplitHorizontalShortcut() const = 0;
    virtual void setEditorSplitHorizontalShortcut(const QString& shortcut) = 0;
    virtual QString editorSplitVerticalShortcut() const = 0;
    virtual void setEditorSplitVerticalShortcut(const QString& shortcut) = 0;
    virtual QString editorFillShortcut() const = 0;
    virtual void setEditorFillShortcut(const QString& shortcut) = 0;
    virtual bool editorGridSnappingEnabled() const = 0;
    virtual void setEditorGridSnappingEnabled(bool enabled) = 0;
    virtual bool editorEdgeSnappingEnabled() const = 0;
    virtual void setEditorEdgeSnappingEnabled(bool enabled) = 0;
    virtual qreal editorSnapIntervalX() const = 0;
    virtual void setEditorSnapIntervalX(qreal interval) = 0;
    virtual qreal editorSnapIntervalY() const = 0;
    virtual void setEditorSnapIntervalY(qreal interval) = 0;
    virtual int editorSnapOverrideModifier() const = 0;
    virtual void setEditorSnapOverrideModifier(int mod) = 0;
    virtual bool fillOnDropEnabled() const = 0;
    virtual void setFillOnDropEnabled(bool enabled) = 0;
    virtual int fillOnDropModifier() const = 0;
    virtual void setFillOnDropModifier(int mod) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-screen overrides — category-keyed maps of setting key → value that
    // live alongside the global setting. Defaults are no-op bodies so backends
    // that don't persist per-screen state (test stubs) can inherit the
    // interface without implementing these. The concrete Settings class
    // overrides every method; the D-Bus SettingsAdaptor depends only on
    // these virtuals so it doesn't need a qobject_cast<Settings*>.
    // The has*() query methods are virtual with a "nothing persisted"
    // default so the SettingsAdaptor can answer existence questions
    // through the interface too.
    // ═══════════════════════════════════════════════════════════════════════════
    virtual QVariantMap getPerScreenAutotileSettings(const QString& /*screenIdOrName*/) const
    {
        return {};
    }
    virtual void setPerScreenAutotileSetting(const QString& /*screenIdOrName*/, const QString& /*key*/,
                                             const QVariant& /*value*/)
    {
    }
    virtual void clearPerScreenAutotileSettings(const QString& /*screenIdOrName*/)
    {
    }
    virtual bool hasPerScreenAutotileSettings(const QString& /*screenIdOrName*/) const
    {
        return false;
    }

    // The three defaults below are spelled `true` rather than calling their
    // ConfigDefaults twins, because this interface header deliberately does
    // not depend on the config layer. A stub answering the opposite of what
    // the real Settings would is a silent behaviour split, so each is pinned
    // from the other side: settings/scrolling.cpp — a TU that sees both —
    // static_asserts the tab-indicator default, the drop-indicator default
    // and ConfigDefaults::scrollingRestoreFloatedWindowsOnLogin() against the
    // literals here, and names this comment. Change any of them and fix both
    // places.

    /// Tab indicator alongside tabbed scrolling columns. Virtual with an
    /// always-on default because two readers reach it through this interface
    /// rather than the concrete Settings: the D-Bus settings registry, which
    /// registers the key through the writer below, and the KWin effect, whose
    /// loadCachedSettings pulls the value across that wire to decide whether
    /// to paint the pills at all (the zoneSelectorEnabled pattern).
    virtual bool scrollingTabIndicatorEnabled() const
    {
        return true;
    }

    /// Writer for the toggle above. Virtual with a no-op default (the
    /// per-screen-accessor pattern) so the D-Bus settings registry can register
    /// the key through the interface rather than only on the concrete Settings
    /// — otherwise a non-Settings backend silently loses the key entirely.
    ///
    /// This is the preferred shape for new keys, but it is not yet the norm:
    /// most global keys are still registered behind a qobject_cast to the
    /// concrete Settings (the REGISTER_CONCRETE_* sites), which accepts exactly
    /// the loss described above. Those are a backlog to hoist, not a second
    /// sanctioned pattern — see the note in settingsadaptor_registry.cpp.
    virtual void setScrollingTabIndicatorEnabled(bool /*enabled*/)
    {
    }

    // The tab indicator's PAINT settings, on this interface for the same
    // reason the toggle above is: the settings registry publishes them and the
    // KWin effect's loadCachedSettings reads them back to paint the pills. The
    // indicator's GEOMETRY settings are deliberately absent: they change the
    // resolved column rect, so the scrolling engine reads them through
    // IScrollSettings and ships the finished rect in the tab-strip payload.
    // Each NUMERIC default below is pinned against its ConfigDefaults twin by a
    // static_assert in settings/scrolling.cpp, the way the toggle above is
    // (that covers the drop-indicator opacity/border-width literals further
    // down too). The five colour defaults cannot be, for two different reasons.
    // The tab trio returns a non-constexpr QString from ConfigDefaults, and
    // test_scrolling_settings.cpp pins their SCHEMA defaults at runtime. The
    // drop-indicator pair below returns a QColor, which is not a literal type
    // either; its agreement with ConfigDefaults is pinned at runtime by
    // dropIndicatorFallbackMatchesInterfaceDefault in test_settings_core.cpp.

    /// 0 = title chips, 1 = segment bar (ConfigDefaults' TabIndicatorStyle).
    /// The bar is the default: it is niri's own indicator, and the only one it
    /// has.
    virtual int scrollingTabIndicatorStyle() const
    {
        return 1;
    }
    virtual void setScrollingTabIndicatorStyle(int /*style*/)
    {
    }
    /// Gap between individual tabs, in logical pixels.
    virtual int scrollingTabIndicatorGapsBetweenTabs() const
    {
        return 0;
    }
    virtual void setScrollingTabIndicatorGapsBetweenTabs(int /*px*/)
    {
    }
    /// Per-tab corner radius in logical pixels; -1 means fully rounded. The
    /// default is square, niri's own; the sentinel is opted into.
    virtual int scrollingTabIndicatorCornerRadius() const
    {
        return 0;
    }
    virtual void setScrollingTabIndicatorCornerRadius(int /*px*/)
    {
    }
    /// Tab colours; empty means "follow the theme" (see ConfigDefaults).
    virtual QString scrollingTabIndicatorActiveColor() const
    {
        return QString();
    }
    virtual void setScrollingTabIndicatorActiveColor(const QString& /*color*/)
    {
    }
    virtual QString scrollingTabIndicatorInactiveColor() const
    {
        return QString();
    }
    virtual void setScrollingTabIndicatorInactiveColor(const QString& /*color*/)
    {
    }
    virtual QString scrollingTabIndicatorUrgentColor() const
    {
        return QString();
    }
    virtual void setScrollingTabIndicatorUrgentColor(const QString& /*color*/)
    {
    }

    /// Drop-target indicator during a scrolling drag re-insert. Virtual with
    /// an always-on default so the overlay service can gate through the
    /// interface, same pattern as scrollingTabIndicatorEnabled above.
    virtual bool scrollingDropIndicatorEnabled() const
    {
        return true;
    }
    virtual void setScrollingDropIndicatorEnabled(bool /*enabled*/)
    {
    }
    /// Fill and border colours. Theme-fallback keys resolved the way the zone
    /// quartet's are (settings_interfaces.h documents that contract): the
    /// getters return RESOLVED colours, palette-derived while the stored string
    /// is empty, and the setters pin a concrete colour, with an INVALID QColor
    /// storing the follow-the-theme sentinel. The stored-string surface lives
    /// on the concrete Settings as scrollingDropIndicator*ColorRaw, for the
    /// settings UI. Resolving here rather than in the overlay's QML keeps the
    /// sentinel inside the config layer, so every consumer of this interface
    /// gets a colour it can paint without knowing the fallback rule.
    ///
    /// The default is the shipped zone highlight at full alpha, which is what
    /// Settings resolves to with no palette to read (ConfigDefaults spells the
    /// same value as scrollingDropIndicatorFallbackColor, unreachable from here
    /// because config depends on core and not the other way round). A backend
    /// that cannot resolve still hands the overlay a paintable colour rather
    /// than the invalid one that renders as black.
    virtual QColor scrollingDropIndicatorColor() const
    {
        return isettings_detail::opaqueDropIndicatorFallback();
    }
    virtual void setScrollingDropIndicatorColor(const QColor& /*color*/)
    {
    }
    virtual QColor scrollingDropIndicatorBorderColor() const
    {
        return isettings_detail::opaqueDropIndicatorFallback();
    }
    virtual void setScrollingDropIndicatorBorderColor(const QColor& /*color*/)
    {
    }
    /// Fill opacity. Applies to the fill only: the border's transparency comes
    /// from its own colour's alpha channel, which nothing downstream replaces.
    virtual double scrollingDropIndicatorOpacity() const
    {
        return 0.25;
    }
    virtual void setScrollingDropIndicatorOpacity(double /*opacity*/)
    {
    }
    virtual int scrollingDropIndicatorBorderWidth() const
    {
        return 2;
    }
    virtual void setScrollingDropIndicatorBorderWidth(int /*px*/)
    {
    }
    /// 8 px, the zone overlay's radius. The static_assert in
    /// settings/scrolling.cpp pins the CONFIGDEFAULTS value, so moving it (or
    /// the ZoneDefaults constant it forwards) fails the build and prompts an
    /// update here. It cannot see a change made on THIS side — editing the
    /// literal below compiles cleanly and drifts silently, so keep the two in
    /// step by hand.
    virtual int scrollingDropIndicatorBorderRadius() const
    {
        return 8;
    }
    virtual void setScrollingDropIndicatorBorderRadius(int /*px*/)
    {
    }

    /// Float-position restore for scroll-floated windows. Virtual with an
    /// always-on default so the WindowTrackingAdaptor's restore predicate
    /// can resolve it through the interface, like its snap/autotile twins.
    virtual bool scrollingRestoreFloatedWindowsOnLogin() const
    {
        return true;
    }

    /// Writer for the toggle above, same no-op-default rationale as
    /// setScrollingTabIndicatorEnabled. The snap/autotile twins are pure virtuals
    /// on IWindowBehaviorSettings; this pair carries defaults because its getters do.
    virtual void setScrollingRestoreFloatedWindowsOnLogin(bool /*restore*/)
    {
    }

    virtual QVariantMap getPerScreenScrollingSettings(const QString& /*screenIdOrName*/) const
    {
        return {};
    }
    virtual void setPerScreenScrollingSetting(const QString& /*screenIdOrName*/, const QString& /*key*/,
                                              const QVariant& /*value*/)
    {
    }
    virtual void clearPerScreenScrollingSettings(const QString& /*screenIdOrName*/)
    {
    }
    virtual bool hasPerScreenScrollingSettings(const QString& /*screenIdOrName*/) const
    {
        return false;
    }

    virtual QVariantMap getPerScreenZoneSelectorSettings(const QString& /*screenIdOrName*/) const
    {
        return {};
    }
    virtual void setPerScreenZoneSelectorSetting(const QString& /*screenIdOrName*/, const QString& /*key*/,
                                                 const QVariant& /*value*/)
    {
    }
    virtual void clearPerScreenZoneSelectorSettings(const QString& /*screenIdOrName*/)
    {
    }
    virtual bool hasPerScreenZoneSelectorSettings(const QString& /*screenIdOrName*/) const
    {
        return false;
    }

    // Strip-mode selector overrides. Separate store from the zone-selector
    // pair above (own group prefix, own map) but the same key vocabulary —
    // see the note on resolvedScrollingZoneSelectorConfig in
    // settings_interfaces.h.
    virtual QVariantMap getPerScreenScrollingZoneSelectorSettings(const QString& /*screenIdOrName*/) const
    {
        return {};
    }
    virtual void setPerScreenScrollingZoneSelectorSetting(const QString& /*screenIdOrName*/, const QString& /*key*/,
                                                          const QVariant& /*value*/)
    {
    }
    virtual void clearPerScreenScrollingZoneSelectorSettings(const QString& /*screenIdOrName*/)
    {
    }
    virtual bool hasPerScreenScrollingZoneSelectorSettings(const QString& /*screenIdOrName*/) const
    {
        return false;
    }

    // NOTE: snapping exposes only the getter — `getPerScreenSnappingSettings`
    // is the lone snapping accessor declared on
    // PhosphorEngine::IGeometrySettings (consumed by the geometry
    // pipeline). It is a PROJECTION, not a store of its own: since the gaps
    // unification the per-monitor gap dimensions are config-backed and live in
    // the per-screen AUTOTILE map (one value per monitor drives both snap and
    // tile), and this getter surfaces that map's gap subset. Hence no
    // set/clear/has triplet, unlike the autotile, scrolling, zone-selector
    // and strip-selector blocks above:
    // writes go through setPerScreenAutotileSetting and the perScreenGap*
    // accessors, and a snapping-side writer would just be a second door onto
    // the same keys.
    QVariantMap getPerScreenSnappingSettings(const QString& /*screenIdOrName*/) const override
    {
        return {};
    }

    // Per-monitor gap overrides (config-backed, unified snap+tile), keyed in the
    // short engine form (InnerGap / OuterGap / UsePerSideOuterGap / OuterGap
    // {Top,Bottom,Left,Right}). Consumed by the geometry cascade merge
    // (GeometryUtils::mergeConfigPerScreenGaps) as the config layer beneath any
    // context gap rule. Default is a no-op empty map so test stubs inherit it.
    virtual QVariantMap perScreenGapOverrides(const QString& /*screenIdOrName*/) const
    {
        return {};
    }

    // Persistence (unique to ISettings)
    //
    // Borrowed-store contract for load(): when the concrete Settings was
    // constructed with an externally-owned `RuleStore*` (e.g. the
    // daemon's shared store), load() MUST NOT reload that store — the owner
    // is the writer and an interleaved load() here would clobber unflushed
    // in-memory edits. Only the owning side (or a future cross-process
    // watcher) drives reloads on the borrowed path. Implementations that
    // own their store (the standard constructor) reload normally. See
    // Settings::load (settings.cpp) for the live guard against
    // `m_ownedRuleStore`.
    virtual void load() = 0;
    /// Persist the current values. Returns false when the write did not
    /// reach disk (the implementation keeps the previous baseline so the
    /// unsaved values stay discardable and the next save retries).
    virtual bool save() = 0;
    /// Restore factory defaults. Returns false when the cleared configuration
    /// could not be persisted, in which case the implementation must leave the
    /// previous state intact rather than half-applying the reset. Callers that
    /// chain further reset work (daemon notification, page bookkeeping) MUST
    /// gate it on this result.
    virtual bool reset() = 0;

Q_SIGNALS:
    void settingsChanged();
    void dragActivationTriggersChanged();
    void autotileDragInsertTriggersChanged();
    void autotileDragInsertToggleChanged();
    void scrollingDragInsertTriggersChanged();
    void scrollingDragInsertToggleChanged();
    void zoneSpanEnabledChanged();
    void zoneSpanModifierChanged();
    void zoneSpanTriggersChanged();
    void zoneSpanToggleModeChanged();
    void toggleActivationChanged();
    void snappingEnabledChanged();
    void showZonesOnAllMonitorsChanged();
    // The per-mode disable signals carry the mode that flipped so listeners can
    // re-read only the relevant list. Pre-v3 these were no-arg signals; the mode
    // argument was added when the storage was split into per-mode lists.
    void disabledMonitorsChanged(PhosphorZones::AssignmentEntry::Mode mode);
    void disabledDesktopsChanged(PhosphorZones::AssignmentEntry::Mode mode);
    void disabledActivitiesChanged(PhosphorZones::AssignmentEntry::Mode mode);
    void showZoneNumbersChanged();
    void flashZonesOnSwitchChanged();
    void showOsdOnLayoutSwitchChanged();
    void showOsdOnDesktopSwitchChanged();
    void showNavigationOsdChanged();
    void osdStyleChanged();
    void overlayDisplayModeChanged();
    // The four zone-colour NOTIFYs fire on a user edit AND on a system
    // palette change while the colour follows the theme (the resolved value
    // moved with no write). An ISettings-holding consumer cannot tell the
    // two apart; the concrete Settings exposes isAnnouncingPaletteChange()
    // for the one consumer that needs to. The drop indicator's fill and
    // border NOTIFYs further down share this dual-source behaviour.
    void highlightColorChanged();
    void inactiveColorChanged();
    void borderColorChanged();
    void labelFontColorChanged();
    void activeOpacityChanged();
    void inactiveOpacityChanged();
    void borderWidthChanged();
    void borderRadiusChanged();
    void labelFontFamilyChanged();
    void labelFontSizeScaleChanged();
    void labelFontWeightChanged();
    void labelFontItalicChanged();
    void labelFontUnderlineChanged();
    void labelFontStrikeoutChanged();
    void innerGapChanged();
    void outerGapChanged();
    void usePerSideOuterGapChanged();
    void outerGapTopChanged();
    void outerGapBottomChanged();
    void outerGapLeftChanged();
    void outerGapRightChanged();
    void adjacentThresholdChanged();
    void pollIntervalMsChanged();
    void minimumZoneSizePxChanged();
    void minimumZoneDisplaySizePxChanged();
    void keepWindowsInZonesOnResolutionChangeChanged();
    void moveNewWindowsToLastZoneChanged();
    void restoreOriginalSizeOnUnsnapChanged();
    void snappingStickyWindowHandlingChanged();
    void restoreWindowsToZonesOnLoginChanged();
    void snappingRestoreFloatedWindowsOnLoginChanged();
    void autotileRestoreFloatedWindowsOnLoginChanged();
    void snapUnfloatFallbackToZoneChanged();
    void autoAssignAllLayoutsChanged();
    void snapAssistFeatureEnabledChanged();
    void snapAssistEnabledChanged();
    void snapAssistTriggersChanged();
    void defaultLayoutIdChanged();
    void suppressDefaultLayoutAssignmentChanged();
    void filterLayoutsByAspectRatioChanged();
    // excludedApplications / excludedWindowClasses signals retired in v4
    // — see settings_interfaces.h for the rationale (lists folded into
    // unified Exclude Rules; consumers subscribe to the rule store
    // through PhosphorRules::RuleStore::rulesChanged instead).
    void excludeTransientWindowsChanged();
    void minimumWindowWidthChanged();
    void minimumWindowHeightChanged();
    // Decoration window filtering — paired with the IWindowExclusionSettings
    // decoration virtuals. Consumed by the kwin-effect (via the generic
    // settingsChanged D-Bus broadcast) and the decorations settings page.
    void decorationExcludeTransientWindowsChanged();
    void decorationMinimumWindowWidthChanged();
    void decorationMinimumWindowHeightChanged();
    // Animation window filtering — paired with the IAnimationSettings
    // virtuals. Same shape as the snapping/tiling exclusion signals; lives
    // in its own change-set so animation-only consumers (the kwin-effect,
    // the AnimationsPageController) don't have to discriminate when
    // filtering NOTIFY traffic.
    void animationExcludeTransientWindowsChanged();
    void animationExcludeNotificationsAndOsdChanged();
    void animationMinimumWindowWidthChanged();
    void animationMinimumWindowHeightChanged();
    // animationExcludedApplicationsChanged / animationExcludedWindowClassesChanged
    // signals retired in v4 alongside the list virtuals above.
    void zoneSelectorEnabledChanged();
    void zoneSelectorTriggerDistanceChanged();
    void zoneSelectorPositionChanged();
    void zoneSelectorLayoutModeChanged();
    void zoneSelectorPreviewWidthChanged();
    void zoneSelectorPreviewHeightChanged();
    void zoneSelectorPreviewLockAspectChanged();
    void zoneSelectorGridColumnsChanged();
    void zoneSelectorSizeModeChanged();
    void zoneSelectorMaxRowsChanged();
    void perScreenZoneSelectorSettingsChanged();
    void scrollingZoneSelectorEnabledChanged();
    void scrollingZoneSelectorTriggerDistanceChanged();
    void scrollingZoneSelectorPositionChanged();
    void scrollingZoneSelectorSizeModeChanged();
    void scrollingZoneSelectorPreviewWidthChanged();
    void scrollingZoneSelectorPreviewHeightChanged();
    void scrollingZoneSelectorPreviewLockAspectChanged();
    void perScreenScrollingZoneSelectorSettingsChanged();
    void perScreenAutotileSettingsChanged();
    void perScreenSnappingSettingsChanged();
    void perScreenScrollingSettingsChanged();
    // Rendering
    void renderingBackendChanged();
    void gpuDeviceChanged();
    // Window decoration appearance (border + title bar)
    void showWindowBorderChanged();
    void windowBorderScopeChanged();
    void windowBorderWidthChanged();
    void windowBorderRadiusChanged();
    void windowBorderColorActiveChanged();
    void windowBorderColorInactiveChanged();
    void hideWindowTitleBarsChanged();
    void windowTitleBarScopeChanged();
    void focusFadeDurationChanged();
    void showWindowOpacityTintChanged();
    void windowOpacityTintScopeChanged();
    void windowOpacityChanged();
    void windowTintStrengthChanged();
    void windowTintColorChanged();
    // Editor
    void editorDuplicateShortcutChanged();
    void editorSplitHorizontalShortcutChanged();
    void editorSplitVerticalShortcutChanged();
    void editorFillShortcutChanged();
    void editorGridSnappingEnabledChanged();
    void editorEdgeSnappingEnabledChanged();
    void editorSnapIntervalXChanged();
    void editorSnapIntervalYChanged();
    void editorSnapOverrideModifierChanged();
    void fillOnDropEnabledChanged();
    void fillOnDropModifierChanged();
    // Shader effects
    void shaderFrameRateChanged();
    void enableAudioVisualizerChanged();
    void audioSpectrumBarCountChanged();
    void audioAutosensChanged();
    void audioSensitivityChanged();
    void audioNoiseReductionChanged();
    void audioLowerCutoffHzChanged();
    void audioHigherCutoffHzChanged();
    void audioMonstercatChanged();
    void audioWavesChanged();
    void audioChannelModeChanged();
    void audioReverseChanged();
    void audioExtraSmoothingChanged();
    void audioInputMethodChanged();
    void audioInputSourceChanged();
    // Global shortcuts
    void openEditorShortcutChanged();
    void openSettingsShortcutChanged();
    void toggleCheatsheetShortcutChanged();
    void previousLayoutShortcutChanged();
    void nextLayoutShortcutChanged();
    void quickLayout1ShortcutChanged();
    void quickLayout2ShortcutChanged();
    void quickLayout3ShortcutChanged();
    void quickLayout4ShortcutChanged();
    void quickLayout5ShortcutChanged();
    void quickLayout6ShortcutChanged();
    void quickLayout7ShortcutChanged();
    void quickLayout8ShortcutChanged();
    void quickLayout9ShortcutChanged();

    // Keyboard Navigation Shortcuts
    void moveWindowLeftShortcutChanged();
    void moveWindowRightShortcutChanged();
    void moveWindowUpShortcutChanged();
    void moveWindowDownShortcutChanged();
    void focusZoneLeftShortcutChanged();
    void focusZoneRightShortcutChanged();
    void focusZoneUpShortcutChanged();
    void focusZoneDownShortcutChanged();
    void pushToEmptyZoneShortcutChanged();
    void restoreWindowSizeShortcutChanged();
    void toggleWindowFloatShortcutChanged();
    void switchFocusFloatTilingShortcutChanged();

    // Swap Window Shortcuts
    void swapWindowLeftShortcutChanged();
    void swapWindowRightShortcutChanged();
    void swapWindowUpShortcutChanged();
    void swapWindowDownShortcutChanged();

    // Span Window Shortcuts
    void spanWindowLeftShortcutChanged();
    void spanWindowRightShortcutChanged();
    void spanWindowUpShortcutChanged();
    void spanWindowDownShortcutChanged();

    // Snap to PhosphorZones::Zone by Number Shortcuts
    void snapToZone1ShortcutChanged();
    void snapToZone2ShortcutChanged();
    void snapToZone3ShortcutChanged();
    void snapToZone4ShortcutChanged();
    void snapToZone5ShortcutChanged();
    void snapToZone6ShortcutChanged();
    void snapToZone7ShortcutChanged();
    void snapToZone8ShortcutChanged();
    void snapToZone9ShortcutChanged();

    // Rotate Windows Shortcuts
    void rotateWindowsClockwiseShortcutChanged();
    void rotateWindowsCounterclockwiseShortcutChanged();

    // Cycle Windows in PhosphorZones::Zone Shortcuts
    void cycleWindowForwardShortcutChanged();
    void cycleWindowBackwardShortcutChanged();

    // Resnap to New PhosphorZones::Layout Shortcut
    void resnapToNewLayoutShortcutChanged();

    // Snap All Windows Shortcut
    void snapAllWindowsShortcutChanged();

    // PhosphorZones::Layout Picker Shortcut
    void layoutPickerShortcutChanged();

    // Toggle PhosphorZones::Layout Lock Shortcut
    void toggleLayoutLockShortcutChanged();

    // Virtual Screen Swap / Rotate Shortcuts
    void swapVirtualScreenLeftShortcutChanged();
    void swapVirtualScreenRightShortcutChanged();
    void swapVirtualScreenUpShortcutChanged();
    void swapVirtualScreenDownShortcutChanged();
    void rotateVirtualScreensClockwiseShortcutChanged();
    void rotateVirtualScreensCounterclockwiseShortcutChanged();

    // Autotile settings
    void autotileEnabledChanged();
    void defaultAutotileAlgorithmChanged();
    void autotileSplitRatioChanged();
    void autotileSplitRatioStepChanged();
    void autotileMasterCountChanged();
    void autotilePerAlgorithmSettingsChanged();
    // Autotile inner/outer gap change signals are unified with snapping —
    // listeners use innerGapChanged / outerGap*Changed above.
    void autotileSmartGapsChanged();
    void autotileMaxWindowsChanged();
    void autotileFocusNewWindowsChanged();
    void autotileInsertPositionChanged();
    void autotileRespectMinimumSizeChanged();
    void autotileFocusFollowsMouseChanged();
    void snappingFocusNewWindowsChanged();
    void snappingFocusFollowsMouseChanged();
    void autotileStickyWindowHandlingChanged();
    void autotileDragBehaviorChanged();
    void autotileOverflowBehaviorChanged();
    void lockedScreensChanged();
    void virtualScreenConfigsChanged();
    // Ordering
    void snappingLayoutOrderChanged();
    void tilingAlgorithmOrderChanged();
    void scrollingTemplateOrderChanged();
    // Animation settings (general)
    void animationsEnabledChanged();
    void animationDurationChanged();
    void animationEasingCurveChanged();
    void animationMinDistanceChanged();
    void animationSequenceModeChanged();
    void animationStaggerIntervalChanged();
    void shaderProfileTreeChanged();

    // Surface decoration settings
    void decorationProfileTreeChanged();
    void decorationAnimateFocusedOnlyChanged();
    void decorationPauseWhenIdleChanged();
    void decorationIdleTimeoutSecChanged();
    void decorationBlurScaleMultiplierChanged();

    // Autotile shortcuts
    void autotileToggleShortcutChanged();
    void autotileRetileShortcutChanged();
    void autotileFocusMasterShortcutChanged();
    void autotileSwapMasterShortcutChanged();
    void autotileIncMasterCountShortcutChanged();
    void autotileDecMasterCountShortcutChanged();
    void autotileIncMasterRatioShortcutChanged();
    void autotileDecMasterRatioShortcutChanged();

    // Scrolling settings
    void scrollingEnabledChanged();
    void scrollingCenterFocusedColumnChanged();
    void scrollingStripAxisChanged();
    void scrollingAlwaysCenterSingleColumnChanged();
    void scrollingCropStraddlersChanged();
    void scrollingDefaultColumnWidthKindChanged();
    void scrollingDefaultColumnWidthValueChanged();
    void scrollingDefaultColumnDisplayChanged();
    void scrollingDefaultColumnWidthPresetIndexChanged();
    void scrollingDefaultWindowHeightKindChanged();
    void scrollingDefaultWindowHeightValueChanged();
    void scrollingDefaultWindowHeightPresetIndexChanged();
    void scrollingPresetColumnWidthsChanged();
    void scrollingPresetWindowHeightsChanged();

    void defaultScrollingTemplateChanged();
    void scrollingWheelFocusEnabledChanged();
    void scrollingWheelFocusInvertedChanged();

    // Scrolling tab indicator (Scrolling.TabIndicator)
    void scrollingTabIndicatorEnabledChanged();
    void scrollingTabIndicatorStyleChanged();
    void scrollingTabIndicatorPositionChanged();
    void scrollingTabIndicatorHideWhenSingleTabChanged();
    void scrollingTabIndicatorPlaceWithinColumnChanged();
    void scrollingTabIndicatorGapChanged();
    void scrollingTabIndicatorWidthChanged();
    void scrollingTabIndicatorLengthProportionChanged();
    void scrollingTabIndicatorGapsBetweenTabsChanged();
    void scrollingTabIndicatorCornerRadiusChanged();
    void scrollingTabIndicatorActiveColorChanged();
    void scrollingTabIndicatorInactiveColorChanged();
    void scrollingTabIndicatorUrgentColorChanged();

    // Scrolling drop indicator (Scrolling.DropIndicator)
    void scrollingDropIndicatorEnabledChanged();
    // These two are dual-source in exactly the way the zone-colour block
    // above describes: a user edit, or a palette change while the colour
    // follows the theme.
    void scrollingDropIndicatorColorChanged();
    void scrollingDropIndicatorBorderColorChanged();
    void scrollingDropIndicatorOpacityChanged();
    void scrollingDropIndicatorBorderWidthChanged();
    void scrollingDropIndicatorBorderRadiusChanged();

    // Scrolling behavior settings
    void scrollingInsertPositionChanged();
    void scrollingFocusNewWindowsChanged();
    void scrollingFocusFollowsMouseChanged();
    void scrollingStickyWindowHandlingChanged();
    void scrollingRespectMinimumSizeChanged();
    void scrollingRestoreStripsOnLoginChanged();
    void scrollingRestoreFloatedWindowsOnLoginChanged();
    void scrollingColumnWidthStepPercentChanged();
    void scrollingWindowHeightStepPercentChanged();

    // Scrolling shortcuts
    void scrollingFocusColumnFirstShortcutChanged();
    void scrollingFocusColumnLastShortcutChanged();
    void scrollingMoveColumnToFirstShortcutChanged();
    void scrollingMoveColumnToLastShortcutChanged();
    void scrollingConsumeWindowShortcutChanged();
    void scrollingExpelWindowShortcutChanged();
    void scrollingConsumeOrExpelLeftShortcutChanged();
    void scrollingConsumeOrExpelRightShortcutChanged();
    void scrollingCenterColumnShortcutChanged();
    void scrollingToggleColumnTabbedShortcutChanged();
    void scrollingToggleWindowedFullscreenShortcutChanged();
    void scrollingCycleColumnWidthShortcutChanged();
    void scrollingCycleColumnWidthBackShortcutChanged();
    void scrollingIncreaseColumnWidthShortcutChanged();
    void scrollingDecreaseColumnWidthShortcutChanged();
    void scrollingMaximizeColumnShortcutChanged();
    void scrollingExpandColumnShortcutChanged();
    void scrollingCycleWindowHeightShortcutChanged();
    void scrollingCycleWindowHeightBackShortcutChanged();
    void scrollingIncreaseWindowHeightShortcutChanged();
    void scrollingDecreaseWindowHeightShortcutChanged();
    void scrollingResetWindowHeightsShortcutChanged();
    void scrollingCenterVisibleColumnsShortcutChanged();
    void scrollingFocusWindowTopShortcutChanged();
    void scrollingFocusWindowBottomShortcutChanged();
    void scrollingFocusColumnLeftShortcutChanged();
    void scrollingFocusColumnRightShortcutChanged();
    void scrollingFocusColumnLeftOrLastShortcutChanged();
    void scrollingFocusColumnRightOrFirstShortcutChanged();
    void scrollingMoveToFloatingShortcutChanged();
    void scrollingMoveToTilingShortcutChanged();

    // ── Environment signals ─────────────────────────────────────────────────
    // Not setting NOTIFYs: nothing above them in the config schema fires these,
    // no Q_PROPERTY is bound to them, and they never touch dirty tracking. They
    // are kept apart from the key-backed run above so that stays a clean
    // one-signal-per-key list.

    /// The system colour scheme flipped between light and dark. Derived from
    /// QEvent::ApplicationPaletteChange in the config layer (the process's one
    /// palette observer). Consumed by the daemon to re-resolve context rules
    /// matching the ColorScheme field and to drop the per-window rule memos
    /// keyed on it; carries no payload because consumers re-read the scheme
    /// through colorSchemeToken() (or the registry's colour-scheme provider).
    void systemColorSchemeChanged();
};

} // namespace PlasmaZones
