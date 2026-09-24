// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
#include "configkeys.h"
#include "perscreenresolver.h"
#include "settings.h"
#include "configmigration_util.h"
#include "configmigration_v4detail.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/QSettingsBackend.h>
#include <PhosphorConfig/Schema.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/IdentityKey.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QLockFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <array>
#include <atomic>
#include <optional>
#include <string_view>

namespace PlasmaZones {

// ── Schema migration: v1 → v2 ───────────────────────────────────────────────
// Restructures flat groups (Activation, Display, etc.) into nested dot-path
// hierarchy (Snapping.Behavior.ZoneSpan, Tiling.Gaps, etc.).

void ConfigMigration::migrateV1ToV2(QJsonObject& root)
{
    // Defense-in-depth idempotency guard. The PhosphorConfig::MigrationRunner
    // already gates this function on `version == 1`, and `ensureJsonConfig`
    // bails early when the on-disk version is >= ConfigSchemaVersion. But
    // a direct caller (test harness, future tooling) that hands us a v2 doc
    // would otherwise read the v2 `Animations` group as if it were v1,
    // remove it, fail to find any v1 keys inside, and end up writing back
    // an empty animations group — silently nuking the user's Profile blob.
    // Bail out before touching the document.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 2) {
        return;
    }

    // ── Read all v1 groups (using ConfigKeys v1 accessors) ────────────────
    const QJsonObject v1Activation = root.value(ConfigKeys::Legacy::v1ActivationGroup()).toObject();
    const QJsonObject v1Display = root.value(ConfigKeys::Legacy::v1DisplayGroup()).toObject();
    const QJsonObject v1Appearance = root.value(ConfigKeys::Legacy::v1AppearanceGroup()).toObject();
    const QJsonObject v1Zones = root.value(ConfigKeys::Legacy::v1ZonesGroup()).toObject();
    const QJsonObject v1Behavior = root.value(ConfigKeys::Legacy::v1BehaviorGroup()).toObject();
    const QJsonObject v1Exclusions = root.value(ConfigKeys::Legacy::v1ExclusionsGroup()).toObject();
    const QJsonObject v1ZoneSelector = root.value(ConfigKeys::Legacy::v1ZoneSelectorGroup()).toObject();
    const QJsonObject v1Autotiling = root.value(ConfigKeys::Legacy::v1AutotilingGroup()).toObject();
    const QJsonObject v1AutotileShortcuts = root.value(ConfigKeys::Legacy::v1AutotileShortcutsGroup()).toObject();
    const QJsonObject v1Animations = root.value(ConfigKeys::Legacy::v1AnimationsGroup()).toObject();
    const QJsonObject v1GlobalShortcuts = root.value(ConfigKeys::Legacy::v1GlobalShortcutsGroup()).toObject();
    const QJsonObject v1Editor = root.value(ConfigKeys::Legacy::v1EditorGroup()).toObject();
    const QJsonObject v1Ordering = root.value(ConfigKeys::Legacy::v1OrderingGroup()).toObject();
    const QJsonObject v1Rendering = root.value(ConfigKeys::Legacy::v1RenderingGroup()).toObject();
    const QJsonObject v1Shaders = root.value(ConfigKeys::Legacy::v1ShadersGroup()).toObject();

    // ── Remove all v1 groups ────────────────────────────────────────────────
    const QString v1Groups[] = {
        ConfigKeys::Legacy::v1ActivationGroup(),        ConfigKeys::Legacy::v1DisplayGroup(),
        ConfigKeys::Legacy::v1AppearanceGroup(),        ConfigKeys::Legacy::v1ZonesGroup(),
        ConfigKeys::Legacy::v1BehaviorGroup(),          ConfigKeys::Legacy::v1ExclusionsGroup(),
        ConfigKeys::Legacy::v1ZoneSelectorGroup(),      ConfigKeys::Legacy::v1AutotilingGroup(),
        ConfigKeys::Legacy::v1AutotileShortcutsGroup(), ConfigKeys::Legacy::v1AnimationsGroup(),
        ConfigKeys::Legacy::v1GlobalShortcutsGroup(),   ConfigKeys::Legacy::v1EditorGroup(),
        ConfigKeys::Legacy::v1OrderingGroup(),          ConfigKeys::Legacy::v1RenderingGroup(),
        ConfigKeys::Legacy::v1ShadersGroup(),
    };
    for (const auto& key : v1Groups) {
        root.remove(key);
    }

    // ── Snapping (top-level) ────────────────────────────────────────────────
    QJsonObject snappingTop;
    moveKey(v1Activation, QLatin1String("SnappingEnabled"), snappingTop, QLatin1String("Enabled"));

    // ── Snapping.Behavior ───────────────────────────────────────────────────
    QJsonObject snappingBehavior;
    moveKey(v1Activation, QLatin1String("DragActivationTriggers"), snappingBehavior, QLatin1String("Triggers"));
    moveKey(v1Activation, QLatin1String("ToggleActivation"), snappingBehavior, QLatin1String("ToggleActivation"));

    // Snapping.Behavior.ZoneSpan
    QJsonObject zoneSpan;
    moveKey(v1Activation, QLatin1String("ZoneSpanEnabled"), zoneSpan, QLatin1String("Enabled"));
    moveKey(v1Activation, QLatin1String("ZoneSpanModifier"), zoneSpan, QLatin1String("Modifier"));
    moveKey(v1Activation, QLatin1String("ZoneSpanTriggers"), zoneSpan, QLatin1String("Triggers"));

    // Snapping.Behavior.SnapAssist
    QJsonObject snapAssist;
    moveKey(v1Activation, QLatin1String("SnapAssistFeatureEnabled"), snapAssist, QLatin1String("FeatureEnabled"));
    moveKey(v1Activation, QLatin1String("SnapAssistEnabled"), snapAssist, QLatin1String("Enabled"));
    moveKey(v1Activation, QLatin1String("SnapAssistTriggers"), snapAssist, QLatin1String("Triggers"));

    // Snapping.Behavior.Display
    QJsonObject snappingDisplay;
    moveKey(v1Display, QLatin1String("ShowOnAllMonitors"), snappingDisplay, QLatin1String("ShowOnAllMonitors"));
    moveKey(v1Display, QLatin1String("DisabledMonitors"), snappingDisplay, QLatin1String("DisabledMonitors"));
    moveKey(v1Display, QLatin1String("DisabledDesktops"), snappingDisplay, QLatin1String("DisabledDesktops"));
    moveKey(v1Display, QLatin1String("DisabledActivities"), snappingDisplay, QLatin1String("DisabledActivities"));
    moveKey(v1Behavior, QLatin1String("FilterLayoutsByAspectRatio"), snappingDisplay,
            QLatin1String("FilterByAspectRatio"));

    // Snapping.Behavior.WindowHandling
    QJsonObject windowHandling;
    moveKey(v1Behavior, QLatin1String("KeepOnResolutionChange"), windowHandling,
            QLatin1String("KeepOnResolutionChange"));
    moveKey(v1Behavior, QLatin1String("MoveNewToLastZone"), windowHandling, QLatin1String("MoveNewToLastZone"));
    moveKey(v1Behavior, QLatin1String("RestoreSizeOnUnsnap"), windowHandling, QLatin1String("RestoreOnUnsnap"));
    moveKey(v1Behavior, QLatin1String("StickyWindowHandling"), windowHandling, QLatin1String("StickyWindowHandling"));
    moveKey(v1Behavior, QLatin1String("RestoreWindowsToZonesOnLogin"), windowHandling, QLatin1String("RestoreOnLogin"));
    moveKey(v1Behavior, QLatin1String("DefaultLayoutId"), windowHandling, QLatin1String("DefaultLayoutId"));

    // Assemble Snapping.Behavior
    if (!zoneSpan.isEmpty())
        snappingBehavior[QLatin1String("ZoneSpan")] = zoneSpan;
    if (!snapAssist.isEmpty())
        snappingBehavior[QLatin1String("SnapAssist")] = snapAssist;
    if (!snappingDisplay.isEmpty())
        snappingBehavior[QLatin1String("Display")] = snappingDisplay;
    if (!windowHandling.isEmpty())
        snappingBehavior[QLatin1String("WindowHandling")] = windowHandling;

    // ── Snapping.Appearance ─────────────────────────────────────────────────
    QJsonObject sColors;
    moveKey(v1Appearance, QLatin1String("UseSystemColors"), sColors, QLatin1String("UseSystem"));
    moveKey(v1Appearance, QLatin1String("HighlightColor"), sColors, QLatin1String("Highlight"));
    moveKey(v1Appearance, QLatin1String("InactiveColor"), sColors, QLatin1String("Inactive"));
    moveKey(v1Appearance, QLatin1String("BorderColor"), sColors, QLatin1String("Border"));

    QJsonObject sOpacity;
    moveKey(v1Appearance, QLatin1String("ActiveOpacity"), sOpacity, QLatin1String("Active"));
    moveKey(v1Appearance, QLatin1String("InactiveOpacity"), sOpacity, QLatin1String("Inactive"));

    QJsonObject sBorder;
    moveKey(v1Appearance, QLatin1String("BorderWidth"), sBorder, QLatin1String("Width"));
    moveKey(v1Appearance, QLatin1String("BorderRadius"), sBorder, QLatin1String("Radius"));

    QJsonObject sLabels;
    moveKey(v1Appearance, QLatin1String("LabelFontColor"), sLabels, QLatin1String("FontColor"));
    moveKey(v1Appearance, QLatin1String("LabelFontFamily"), sLabels, QLatin1String("FontFamily"));
    moveKey(v1Appearance, QLatin1String("LabelFontSizeScale"), sLabels, QLatin1String("FontSizeScale"));
    moveKey(v1Appearance, QLatin1String("LabelFontWeight"), sLabels, QLatin1String("FontWeight"));
    moveKey(v1Appearance, QLatin1String("LabelFontItalic"), sLabels, QLatin1String("FontItalic"));
    moveKey(v1Appearance, QLatin1String("LabelFontUnderline"), sLabels, QLatin1String("FontUnderline"));
    moveKey(v1Appearance, QLatin1String("LabelFontStrikeout"), sLabels, QLatin1String("FontStrikeout"));

    QJsonObject snappingAppearance;
    if (!sColors.isEmpty())
        snappingAppearance[QLatin1String("Colors")] = sColors;
    if (!sOpacity.isEmpty())
        snappingAppearance[QLatin1String("Opacity")] = sOpacity;
    if (!sBorder.isEmpty())
        snappingAppearance[QLatin1String("Border")] = sBorder;
    if (!sLabels.isEmpty())
        snappingAppearance[QLatin1String("Labels")] = sLabels;

    // ── Snapping.Effects ────────────────────────────────────────────────────
    QJsonObject effects;
    // v1 "EnableBlur" is intentionally not migrated: the blur setting was
    // retired (no runtime consumer), so the old value is silently dropped.
    moveKey(v1Display, QLatin1String("ShowNumbers"), effects, QLatin1String("ShowNumbers"));
    moveKey(v1Display, QLatin1String("FlashOnSwitch"), effects, QLatin1String("FlashOnSwitch"));
    moveKey(v1Display, QLatin1String("ShowOsdOnLayoutSwitch"), effects, QLatin1String("OsdOnLayoutSwitch"));
    moveKey(v1Display, QLatin1String("ShowNavigationOsd"), effects, QLatin1String("NavigationOsd"));
    moveKey(v1Display, QLatin1String("OsdStyle"), effects, QLatin1String("OsdStyle"));
    moveKey(v1Display, QLatin1String("OverlayDisplayMode"), effects, QLatin1String("OverlayDisplayMode"));

    // ── Snapping.ZoneSelector (keys mostly unchanged) ───────────────────────
    // v1 ZoneSelector keys don't have prefixes, so they stay the same
    QJsonObject zoneSelector = v1ZoneSelector;

    // ── Snapping.Gaps ─────────────────────────────────────────────────────────
    // The shared inner/outer gaps are NOT migrated from v1. Per the
    // no-ad-hoc-backwards-compat policy the ancient v1 zone-padding / outer-gap
    // values are silently dropped (left in v1Zones, never written out); a v1 user
    // falls back to the config gap defaults (the top-level "Gaps" group, populated
    // by the v4→v5 step). The snapping-specific AdjacentThreshold stays in
    // Snapping.Gaps.
    QJsonObject snappingGaps;
    moveKey(v1Zones, QLatin1String("AdjacentThreshold"), snappingGaps, QLatin1String("AdjacentThreshold"));

    // ── Assemble Snapping ───────────────────────────────────────────────────
    QJsonObject snapping = snappingTop;
    if (!snappingBehavior.isEmpty())
        snapping[QLatin1String("Behavior")] = snappingBehavior;
    if (!snappingAppearance.isEmpty())
        snapping[QLatin1String("Appearance")] = snappingAppearance;
    if (!effects.isEmpty())
        snapping[QLatin1String("Effects")] = effects;
    if (!zoneSelector.isEmpty())
        snapping[QLatin1String("ZoneSelector")] = zoneSelector;
    if (!snappingGaps.isEmpty())
        snapping[QLatin1String("Gaps")] = snappingGaps;
    if (!snapping.isEmpty())
        root[ConfigKeys::Legacy::v2SnappingGroup()] = snapping;

    // ── Performance ─────────────────────────────────────────────────────────
    QJsonObject performance;
    moveKey(v1Zones, QLatin1String("PollIntervalMs"), performance, QLatin1String("PollIntervalMs"));
    moveKey(v1Zones, QLatin1String("MinimumZoneSizePx"), performance, QLatin1String("MinimumZoneSizePx"));
    moveKey(v1Zones, QLatin1String("MinimumZoneDisplaySizePx"), performance, QLatin1String("MinimumZoneDisplaySizePx"));
    if (!performance.isEmpty())
        root[ConfigKeys::Legacy::v2PerformanceGroup()] = performance;

    // ── Tiling ──────────────────────────────────────────────────────────────
    QJsonObject tilingTop;
    moveKey(v1Autotiling, QLatin1String("AutotileEnabled"), tilingTop, QLatin1String("Enabled"));

    QJsonObject tilingAlgo;
    moveKey(v1Autotiling, QLatin1String("DefaultAutotileAlgorithm"), tilingAlgo, QLatin1String("Default"));
    moveKey(v1Autotiling, QLatin1String("AutotileSplitRatio"), tilingAlgo, QLatin1String("SplitRatio"));
    moveKey(v1Autotiling, QLatin1String("AutotileSplitRatioStep"), tilingAlgo, QLatin1String("SplitRatioStep"));
    moveKey(v1Autotiling, QLatin1String("AutotileMasterCount"), tilingAlgo, QLatin1String("MasterCount"));
    moveKey(v1Autotiling, QLatin1String("AutotileMaxWindows"), tilingAlgo, QLatin1String("MaxWindows"));
    moveKey(v1Autotiling, QLatin1String("AutotilePerAlgorithmSettings"), tilingAlgo,
            QLatin1String("PerAlgorithmSettings"));

    QJsonObject tilingBehavior;
    moveKey(v1Autotiling, QLatin1String("AutotileInsertPosition"), tilingBehavior, QLatin1String("InsertPosition"));
    moveKey(v1Autotiling, QLatin1String("AutotileFocusNewWindows"), tilingBehavior, QLatin1String("FocusNewWindows"));
    moveKey(v1Autotiling, QLatin1String("AutotileFocusFollowsMouse"), tilingBehavior,
            QLatin1String("FocusFollowsMouse"));
    moveKey(v1Autotiling, QLatin1String("AutotileRespectMinimumSize"), tilingBehavior,
            QLatin1String("RespectMinimumSize"));
    moveKey(v1Autotiling, QLatin1String("AutotileStickyWindowHandling"), tilingBehavior,
            QLatin1String("StickyWindowHandling"));
    moveKey(v1Autotiling, QLatin1String("LockedScreens"), tilingBehavior, QLatin1String("LockedScreens"));

    // The v1 autotile border / title-bar appearance keys
    // (AutotileShowBorder / Width / Radius / BorderColor / InactiveBorderColor /
    // UseSystemBorderColors / HideTitleBars) are intentionally dropped. Per the
    // no-ad-hoc-backwards-compat policy the ancient v1 values are silently
    // discarded; a v1 user falls back to the config appearance defaults (the
    // top-level "Windows" group, populated by the v4→v5 step).

    // The v1 autotile inner/outer gap keys are dropped for the same reason as the
    // snapping ones above (no-ad-hoc-backwards-compat; users fall back to the
    // config "Gaps" defaults). SmartGaps is tiling-specific and stays in Tiling.Gaps.
    QJsonObject tilingGaps;
    moveKey(v1Autotiling, QLatin1String("AutotileSmartGaps"), tilingGaps, QLatin1String("SmartGaps"));

    QJsonObject tiling = tilingTop;
    if (!tilingAlgo.isEmpty())
        tiling[QLatin1String("Algorithm")] = tilingAlgo;
    if (!tilingBehavior.isEmpty())
        tiling[QLatin1String("Behavior")] = tilingBehavior;
    if (!tilingGaps.isEmpty())
        tiling[QLatin1String("Gaps")] = tilingGaps;
    if (!tiling.isEmpty())
        root[ConfigKeys::Legacy::v2TilingGroup()] = tiling;

    // ── Exclusions (key renames) ────────────────────────────────────────────
    QJsonObject exclusions;
    moveKey(v1Exclusions, QLatin1String("ExcludeTransientWindows"), exclusions, QLatin1String("TransientWindows"));
    moveKey(v1Exclusions, QLatin1String("MinimumWindowWidth"), exclusions, QLatin1String("MinimumWindowWidth"));
    moveKey(v1Exclusions, QLatin1String("MinimumWindowHeight"), exclusions, QLatin1String("MinimumWindowHeight"));
    moveKey(v1Exclusions, QLatin1String("Applications"), exclusions, QLatin1String("Applications"));
    moveKey(v1Exclusions, QLatin1String("WindowClasses"), exclusions, QLatin1String("WindowClasses"));
    if (!exclusions.isEmpty())
        root[ConfigKeys::Legacy::v2ExclusionsGroup()] = exclusions;

    // ── Rendering (key rename) ──────────────────────────────────────────────
    QJsonObject rendering;
    moveKey(v1Rendering, ConfigKeys::Legacy::v1RenderingBackendKey(), rendering, QLatin1String("Backend"));
    if (!rendering.isEmpty())
        root[ConfigKeys::Legacy::v2RenderingGroup()] = rendering;

    // ── Shaders (key renames) ───────────────────────────────────────────────
    QJsonObject shaders;
    moveKey(v1Shaders, QLatin1String("EnableShaderEffects"), shaders, QLatin1String("Enabled"));
    moveKey(v1Shaders, QLatin1String("ShaderFrameRate"), shaders, QLatin1String("FrameRate"));
    moveKey(v1Shaders, QLatin1String("EnableAudioVisualizer"), shaders, QLatin1String("AudioVisualizer"));
    moveKey(v1Shaders, QLatin1String("AudioSpectrumBarCount"), shaders, QLatin1String("AudioSpectrumBarCount"));
    if (!shaders.isEmpty())
        root[ConfigKeys::Legacy::v2ShadersGroup()] = shaders;

    // ── Animations (key renames + Phase-4 Profile-blob packing) ────────────
    // v1 stored five per-field animation keys + a standalone `AnimationsEnabled`
    // bool. v2's Phase-4-restructure packs the five per-field values into a
    // single `Profile` JSON blob (decision S) while keeping `Enabled` as an
    // orthogonal bool. Preserve v1 users' customisation by composing the
    // Profile blob inline here rather than dropping the fields.
    //
    // Both sides go through frozen ConfigKeys::Legacy accessors (per CLAUDE.md
    // rule: no inline QStringLiteral for config keys): the destination via
    // v2AnimationsGroup / v2AnimationsEnabledKey / v2AnimationProfileKey —
    // frozen at the v2 on-disk spellings, like every sibling destination in
    // this step, so a future rename of the live ConfigDefaults accessors
    // cannot silently retarget the migration — and the source via
    // ConfigKeys::Legacy::v1Animation*Key() so the migration is unambiguous about
    // "reading legacy field" — the v1 shape is stable by definition but
    // having a single source of truth keeps `grep "AnimationDuration"`
    // returning one accessor declaration instead of N call-sites.
    QJsonObject animations;
    moveKey(v1Animations, ConfigKeys::Legacy::v1AnimationsEnabledKey(), animations,
            ConfigKeys::Legacy::v2AnimationsEnabledKey());

    // Assemble Profile fields from the v1 keys (if present). We build
    // the JSON shape directly using Profile's public field-name
    // constants — matches `Profile::toJson` output without pulling
    // phosphor-animation runtime objects into the migration path
    // (which would bloat the daemon-startup dependency graph for a
    // transient code path). Sharing the constants guarantees that a
    // Profile field rename touches producer and migration together.
    // Every clampable field goes through `qBound` against the
    // ConfigDefaults Min/Max accessors. A v1 config with, say,
    // `AnimationDuration=-50` or `AnimationStaggerInterval=600000` would
    // otherwise land in the v2 blob verbatim — and `Profile::fromJson`
    // rejects out-of-range values at load time with a warning, so the
    // user silently loses their customisation AND the log gets noisy.
    // Clamping at migration time prevents both: the stored value is
    // always in-range, and `Profile::fromJson` accepts it cleanly.
    QJsonObject profile;
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationDurationKey())) {
        const int raw = v1Animations.value(ConfigKeys::Legacy::v1AnimationDurationKey()).toInt();
        const int clamped = qBound(ConfigDefaults::animationDurationMin(), raw, ConfigDefaults::animationDurationMax());
        profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration)] = clamped;
    }
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationEasingCurveKey())) {
        // Resolve the v1 friendly name (e.g. "easeOutCubic") through a
        // stack-local CurveRegistry so we store the canonical wire form
        // (e.g. "0.33,1.00,0.68,1.00") in the Profile blob. Without this
        // step, `Profile::fromJson` resolves the friendly name on read
        // via the consumer's registry, but the first UI save then
        // serialises the Curve back using `Curve::toString()` — which
        // always emits canonical wire form — causing a spurious config
        // rewrite on first post-migration interaction. Built-in curves
        // auto-register via the CurveRegistry constructor, so no
        // explicit registerBuiltins() call is needed here.
        //
        // Unknown specs (custom curve names from a v1 plugin that no
        // longer exists, typos in hand-edited v1 configs) are DROPPED
        // here rather than persisted. Persisting the raw string would
        // make `Profile::fromJson` emit the "curve spec … did not
        // resolve" warning on every daemon start forever; dropping the
        // field lets the library-default OutCubic apply and silences
        // the repeating diagnostic. The migration log below records
        // the dropped spec so an operator investigating a silent
        // curve change can find it.
        const QString v1Curve = v1Animations.value(ConfigKeys::Legacy::v1AnimationEasingCurveKey()).toString();
        PhosphorAnimation::CurveRegistry registry;
        if (const auto resolved = registry.tryCreate(v1Curve)) {
            profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve)] = resolved->toString();
        } else {
            qInfo("migrateV1ToV2: dropping unresolved v1 curve spec '%s' — library default (OutCubic) will apply",
                  qPrintable(v1Curve));
        }
    }
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationMinDistanceKey())) {
        const int raw = v1Animations.value(ConfigKeys::Legacy::v1AnimationMinDistanceKey()).toInt();
        const int clamped =
            qBound(ConfigDefaults::animationMinDistanceMin(), raw, ConfigDefaults::animationMinDistanceMax());
        profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldMinDistance)] = clamped;
    }
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationSequenceModeKey())) {
        // SequenceMode is a closed enum (AllAtOnce=0, Stagger=1 as of v2).
        // Out-of-range values snap to the project default rather than
        // clamping to the nearest bound — clamping would silently alias
        // e.g. 999 onto Stagger, which is semantically different from
        // "the user's setting is meaningless, use the default".
        const int raw = v1Animations.value(ConfigKeys::Legacy::v1AnimationSequenceModeKey()).toInt();
        const int resolved =
            (raw >= ConfigDefaults::animationSequenceModeMin() && raw <= ConfigDefaults::animationSequenceModeMax())
            ? raw
            : ConfigDefaults::animationSequenceMode();
        profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldSequenceMode)] = resolved;
    }
    if (v1Animations.contains(ConfigKeys::Legacy::v1AnimationStaggerIntervalKey())) {
        const int raw = v1Animations.value(ConfigKeys::Legacy::v1AnimationStaggerIntervalKey()).toInt();
        const int clamped =
            qBound(ConfigDefaults::animationStaggerIntervalMin(), raw, ConfigDefaults::animationStaggerIntervalMax());
        profile[QLatin1String(PhosphorAnimation::Profile::JsonFieldStaggerInterval)] = clamped;
    }
    if (!profile.isEmpty()) {
        // v1→v2 writes a stringified JSON blob even though the live schema
        // now stores the animation profile as a nested QVariantMap.
        // Stringifying here keeps the migrated value as a single scalar
        // leaf so the schema/migration cross-check
        // (`testSchemaCoversEveryMigrationDestinationKey`) sees one
        // declared key (`Animations/Profile` — `animationProfileKey()`
        // returns "Profile") instead of treating the nested object as a
        // sub-group of synthetic per-field keys.
        // The Settings layer's `Store::read<QVariantMap>` legacy-string
        // fallback parses this on first load and the next save normalises
        // it to a nested object.
        animations[ConfigKeys::Legacy::v2AnimationProfileKey()] =
            QString::fromUtf8(QJsonDocument(profile).toJson(QJsonDocument::Compact));
    }
    if (!animations.isEmpty())
        root[ConfigKeys::Legacy::v2AnimationsGroup()] = animations;

    // ── Shortcuts.Global (drop "Shortcut" suffix from some keys) ────────────
    QJsonObject globalShortcuts;
    moveKey(v1GlobalShortcuts, QLatin1String("OpenEditorShortcut"), globalShortcuts, QLatin1String("OpenEditor"));
    moveKey(v1GlobalShortcuts, QLatin1String("OpenSettingsShortcut"), globalShortcuts, QLatin1String("OpenSettings"));
    moveKey(v1GlobalShortcuts, QLatin1String("PreviousLayoutShortcut"), globalShortcuts,
            QLatin1String("PreviousLayout"));
    moveKey(v1GlobalShortcuts, QLatin1String("NextLayoutShortcut"), globalShortcuts, QLatin1String("NextLayout"));
    for (int i = 1; i <= 9; ++i) {
        moveKey(v1GlobalShortcuts, ConfigKeys::Legacy::v1QuickLayoutShortcutKey(i), globalShortcuts,
                ConfigKeys::Legacy::v2QuickLayoutKey(i));
    }
    // Navigation keys — same names in v1 and v2
    for (const auto& key :
         {"MoveWindowLeft", "MoveWindowRight", "MoveWindowUp", "MoveWindowDown", "FocusZoneLeft", "FocusZoneRight",
          "FocusZoneUp", "FocusZoneDown", "PushToEmptyZone", "RestoreWindowSize", "ToggleWindowFloat", "SwapWindowLeft",
          "SwapWindowRight", "SwapWindowUp", "SwapWindowDown", "RotateWindowsClockwise",
          "RotateWindowsCounterclockwise", "CycleWindowForward", "CycleWindowBackward"}) {
        moveKey(v1GlobalShortcuts, QLatin1String(key), globalShortcuts, QLatin1String(key));
    }
    for (int i = 1; i <= 9; ++i) {
        const QString key = ConfigKeys::Legacy::v2SnapToZoneKey(i);
        moveKey(v1GlobalShortcuts, key, globalShortcuts, key);
    }
    moveKey(v1GlobalShortcuts, QLatin1String("ResnapToNewLayoutShortcut"), globalShortcuts,
            QLatin1String("ResnapToNewLayout"));
    moveKey(v1GlobalShortcuts, QLatin1String("SnapAllWindowsShortcut"), globalShortcuts,
            QLatin1String("SnapAllWindows"));
    moveKey(v1GlobalShortcuts, QLatin1String("LayoutPickerShortcut"), globalShortcuts, QLatin1String("LayoutPicker"));
    moveKey(v1GlobalShortcuts, QLatin1String("ToggleLayoutLockShortcut"), globalShortcuts,
            QLatin1String("ToggleLayoutLock"));

    // ── Shortcuts.Tiling (drop "Shortcut" suffix) ───────────────────────────
    QJsonObject tilingShortcuts;
    moveKey(v1AutotileShortcuts, QLatin1String("ToggleShortcut"), tilingShortcuts, QLatin1String("Toggle"));
    moveKey(v1AutotileShortcuts, QLatin1String("FocusMasterShortcut"), tilingShortcuts, QLatin1String("FocusMaster"));
    moveKey(v1AutotileShortcuts, QLatin1String("SwapMasterShortcut"), tilingShortcuts, QLatin1String("SwapMaster"));
    moveKey(v1AutotileShortcuts, QLatin1String("IncMasterRatioShortcut"), tilingShortcuts,
            QLatin1String("IncMasterRatio"));
    moveKey(v1AutotileShortcuts, QLatin1String("DecMasterRatioShortcut"), tilingShortcuts,
            QLatin1String("DecMasterRatio"));
    moveKey(v1AutotileShortcuts, QLatin1String("IncMasterCountShortcut"), tilingShortcuts,
            QLatin1String("IncMasterCount"));
    moveKey(v1AutotileShortcuts, QLatin1String("DecMasterCountShortcut"), tilingShortcuts,
            QLatin1String("DecMasterCount"));
    moveKey(v1AutotileShortcuts, QLatin1String("RetileShortcut"), tilingShortcuts, QLatin1String("Retile"));

    QJsonObject shortcuts;
    if (!globalShortcuts.isEmpty())
        shortcuts[QLatin1String("Global")] = globalShortcuts;
    if (!tilingShortcuts.isEmpty())
        shortcuts[QLatin1String("Tiling")] = tilingShortcuts;
    if (!shortcuts.isEmpty())
        root[ConfigKeys::Legacy::v2ShortcutsGroup()] = shortcuts;

    // ── Editor (split into sub-groups) ──────────────────────────────────────
    QJsonObject editorShortcuts;
    moveKey(v1Editor, QLatin1String("EditorDuplicateShortcut"), editorShortcuts, QLatin1String("Duplicate"));
    moveKey(v1Editor, QLatin1String("EditorSplitHorizontalShortcut"), editorShortcuts,
            QLatin1String("SplitHorizontal"));
    moveKey(v1Editor, QLatin1String("EditorSplitVerticalShortcut"), editorShortcuts, QLatin1String("SplitVertical"));
    moveKey(v1Editor, QLatin1String("EditorFillShortcut"), editorShortcuts, QLatin1String("Fill"));

    QJsonObject editorSnapping;
    moveKey(v1Editor, QLatin1String("GridSnappingEnabled"), editorSnapping, QLatin1String("GridEnabled"));
    moveKey(v1Editor, QLatin1String("EdgeSnappingEnabled"), editorSnapping, QLatin1String("EdgeEnabled"));
    moveKey(v1Editor, QLatin1String("SnapIntervalX"), editorSnapping, QLatin1String("IntervalX"));
    moveKey(v1Editor, QLatin1String("SnapIntervalY"), editorSnapping, QLatin1String("IntervalY"));
    // If per-axis intervals don't exist, propagate the unified SnapInterval to both.
    // Without this, configs that only set SnapInterval (no per-axis override) would
    // lose their value because the v2 load code reads IntervalX/IntervalY directly.
    if (!editorSnapping.contains(QLatin1String("IntervalX"))) {
        moveKey(v1Editor, QLatin1String("SnapInterval"), editorSnapping, QLatin1String("IntervalX"));
    }
    if (!editorSnapping.contains(QLatin1String("IntervalY"))) {
        moveKey(v1Editor, QLatin1String("SnapInterval"), editorSnapping, QLatin1String("IntervalY"));
    }
    moveKey(v1Editor, QLatin1String("SnapOverrideModifier"), editorSnapping, QLatin1String("OverrideModifier"));

    QJsonObject editorFillOnDrop;
    moveKey(v1Editor, QLatin1String("FillOnDropEnabled"), editorFillOnDrop, QLatin1String("Enabled"));
    moveKey(v1Editor, QLatin1String("FillOnDropModifier"), editorFillOnDrop, QLatin1String("Modifier"));

    QJsonObject editor;
    if (!editorShortcuts.isEmpty())
        editor[QLatin1String("Shortcuts")] = editorShortcuts;
    if (!editorSnapping.isEmpty())
        editor[QLatin1String("Snapping")] = editorSnapping;
    if (!editorFillOnDrop.isEmpty())
        editor[QLatin1String("FillOnDrop")] = editorFillOnDrop;
    if (!editor.isEmpty())
        root[ConfigKeys::Legacy::v2EditorGroup()] = editor;

    // ── Ordering (keys unchanged) ───────────────────────────────────────────
    if (!v1Ordering.isEmpty())
        root[ConfigKeys::Legacy::v2OrderingGroup()] = v1Ordering;

    // ── Extract WindowTracking (ephemeral session state) to session.json ──
    // In v2, session state lives in its own file to avoid write contention
    // between user preferences (config.json) and high-frequency window
    // tracking saves (session.json).
    //
    // Atomicity: only mutate `root` after the side-effect write succeeds.
    // If the out-of-tree write fails, leaving the keys in `root` lets
    // `config.json` retain the data so the next startup's migration can
    // retry. A partial commit here would silently lose session state.
    bool allSideEffectsSucceeded = true;

    // Source read uses the frozen v1 group accessor per the configkeys.h
    // freeze policy — a future runtime rename of `windowTrackingGroup`
    // must not silently miss this read. Destination write into
    // session.json's root uses the live runtime group accessor — that
    // file is the live shape's home.
    const QString srcWtGroup = ConfigKeys::Legacy::v1WindowTrackingGroup();
    const QString dstWtGroup = ConfigKeys::windowTrackingGroup();
    if (root.contains(srcWtGroup)) {
        QJsonObject sessionRoot;
        sessionRoot[dstWtGroup] = root.value(srcWtGroup);
        const QString sessionPath = ConfigDefaults::sessionFilePath();
        if (PhosphorConfig::JsonBackend::writeJsonAtomically(sessionPath, sessionRoot)) {
            root.remove(srcWtGroup);
        } else {
            qWarning(
                "ConfigMigration: failed to write session state to %s — "
                "aborting migration so the next run can retry",
                qPrintable(sessionPath));
            allSideEffectsSucceeded = false;
        }
    }

    // ── Extract Assignment/QuickLayouts to assignments.json ─────────────────
    // assignments.json is the v3 PhosphorZones::LayoutRegistry persistence file. It is
    // itself superseded in v4 by rules.json (see finalizeV4Conversion),
    // so this extraction is a stepping-stone that v3→v4 reads back out.
    {
        QJsonObject assignRoot;
        const QString assignPrefix = ConfigKeys::Legacy::v3assignmentGroupPrefix();
        QStringList keysToRemove;
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (it.key().startsWith(assignPrefix)) {
                assignRoot[it.key()] = it.value();
                keysToRemove.append(it.key());
            }
        }
        const QString quickLayoutsKey = ConfigKeys::Legacy::v3quickLayoutsGroup();
        if (root.contains(quickLayoutsKey)) {
            assignRoot[quickLayoutsKey] = root.value(quickLayoutsKey);
            keysToRemove.append(quickLayoutsKey);
        }
        // ModeTracking is NOT extracted to assignments.json — in v3 it was
        // consumed by PhosphorZones::LayoutRegistry directly from config.json and deleted
        // after application. Extracting it would leave dead data in
        // assignments.json that nothing reads (and v4 doesn't read it either).
        const QString modeTrackingKey = ConfigKeys::Legacy::v3modeTrackingGroup();
        if (root.contains(modeTrackingKey)) {
            keysToRemove.append(modeTrackingKey);
        }

        // "Commit-ok": true when the assignments file was successfully
        // written, OR when there was nothing to write in the first place.
        // Drives the keysToRemove drain below — only strip from root once
        // the external destination is durable (or vacuously durable).
        bool assignmentsCommitOk = true;
        if (!assignRoot.isEmpty()) {
            const QString assignPath = legacyAssignmentsFilePath();
            if (!PhosphorConfig::JsonBackend::writeJsonAtomically(assignPath, assignRoot)) {
                qWarning(
                    "ConfigMigration: failed to write assignments to %s — "
                    "aborting migration so the next run can retry",
                    qPrintable(assignPath));
                assignmentsCommitOk = false;
                allSideEffectsSucceeded = false;
            }
        }
        // Only strip from root if the external file is committed (or there
        // was nothing to extract). ModeTracking is safe to drop unconditionally
        // — it's ephemeral and consumed in-memory only.
        if (assignmentsCommitOk) {
            for (const QString& key : keysToRemove) {
                root.remove(key);
            }
        } else if (root.contains(modeTrackingKey)) {
            // Still safe to drop ModeTracking even if assignments write failed.
            root.remove(modeTrackingKey);
        }
    }

    // ── Bump version ────────────────────────────────────────────────────────
    // Stamp literal 2, not ConfigSchemaVersion — prevents future version bumps
    // (e.g. to 3) from making this step stamp 3 and skipping a v2→v3 migration.
    //
    // Skip the bump when any side-effect write failed. MigrationRunner
    // detects the unbumped version, aborts the chain with a critical log,
    // and config.json is left untouched so the next startup retries.
    //
    // Note: the in-memory @p root has already been mutated extensively
    // above (v1 groups removed, dot-path hierarchy rebuilt) by the time
    // we get here. On a side-effect failure these mutations are silently
    // discarded by the caller — MigrationRunner::runOnFile sees that the
    // version key didn't advance and skips the disk write entirely, and
    // Store::Store's in-memory migration path likewise compares before vs.
    // after and skips the file rewrite. The on-disk file is therefore left
    // at v1 with the original layout, ready for the next startup to retry.
    if (allSideEffectsSucceeded) {
        root[ConfigKeys::versionKey()] = 2;
    }
}

} // namespace PlasmaZones
