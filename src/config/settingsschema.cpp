// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsschema.h"

#include "configdefaults.h"
#include "configmigration.h"
#include "../autotile/AutotileConfig.h"
#include "../core/enums.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <QtGlobal>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

PhosphorConfig::Schema buildSettingsSchema()
{
    PhosphorConfig::Schema s;
    s.version = ConfigSchemaVersion;
    s.versionKey = ConfigKeys::versionKey();

    appendShadersSchema(s);
    appendAppearanceSchema(s);
    appendOrderingSchema(s);
    appendAnimationsSchema(s);
    appendRenderingSchema(s);
    appendPerformanceSchema(s);
    appendZoneGeometrySchema(s);
    appendShortcutsSchema(s);
    appendEditorSchema(s);
    appendExclusionsSchema(s);
    appendDisplaySchema(s);
    appendZoneSelectorSchema(s);
    appendActivationSchema(s);
    appendBehaviorSchema(s);
    appendAutotilingSchema(s);

    return s;
}

// ─── Validator helpers ──────────────────────────────────────────────────────
// Common coercion patterns factored to keep group schemas readable. Return
// the same function-object type as KeyDef::validator.

namespace {
auto clampInt(int minVal, int maxVal)
{
    return [minVal, maxVal](const QVariant& v) -> QVariant {
        return qBound(minVal, v.toInt(), maxVal);
    };
}

auto clampDouble(double minVal, double maxVal)
{
    return [minVal, maxVal](const QVariant& v) -> QVariant {
        return qBound(minVal, v.toDouble(), maxVal);
    };
}

/// Fall back to @p fallback when the value is an invalid color. Defaults
/// in the schema are already valid, so this mostly protects against
/// garbage in the on-disk file.
auto validColorOr(QColor fallback)
{
    return [fallback](const QVariant& v) -> QVariant {
        const QColor c = v.value<QColor>();
        return c.isValid() ? QVariant::fromValue(c) : QVariant::fromValue(fallback);
    };
}

/// Snap-to-default enum validator: accept a value only if it appears in the
/// explicit valid set, otherwise return @p fallback. Used for enums where
/// qBound would silently reinterpret out-of-range values as the nearest
/// neighbour — that's the exact bug the effect-side cache loader avoids,
/// and both readers must agree (see testAutotile*_unknownValueClampsToFloat).
auto validIntOr(std::initializer_list<int> valid, int fallback)
{
    return [valid = QVector<int>(valid), fallback](const QVariant& v) -> QVariant {
        const int raw = v.toInt();
        return valid.contains(raw) ? raw : fallback;
    };
}

/// Normalize a comma-joined list: split, trim each entry, drop empties,
/// drop duplicates, rejoin. Shared by every setting whose wire format is a
/// comma-separated list (layout order, algorithm order, exclusion lists).
QVariant canonicalCommaList(const QVariant& v)
{
    QStringList parts = v.toString().split(QLatin1Char(','));
    for (auto& s : parts) {
        s = s.trimmed();
    }
    parts.removeAll(QString());
    parts.removeDuplicates();
    return QVariant(parts.join(QLatin1Char(',')));
}

/// Canonicalize a trigger list: cap size, coerce each entry to a
/// {modifier:int, mouseButton:int} QVariantMap. Runs on every read and
/// every write so the flush loop enforces the cap even when the setter
/// path is bypassed (e.g. a hand-edited config file carrying 12 entries).
///
/// Both this file's cap and Settings::MaxTriggersPerAction resolve to
/// ConfigDefaults::maxTriggersPerAction() — single source of truth, no
/// drift possible because neither TU carries its own literal.
constexpr int kSchemaMaxTriggersPerAction = ConfigDefaults::maxTriggersPerAction();

QVariant canonicalTriggerList(const QVariant& v)
{
    const QVariantList raw = v.toList();
    QVariantList out;
    out.reserve(qMin(raw.size(), kSchemaMaxTriggersPerAction));
    for (const QVariant& entry : raw) {
        if (out.size() >= kSchemaMaxTriggersPerAction) {
            break;
        }
        // Skip non-map entries instead of coercing them to zero-valued
        // triggers — matches the legacy reader's "drop malformed" behaviour
        // so a corrupt config with a string element can't smuggle a
        // {modifier:0, mouseButton:0} phantom trigger in.
        if (entry.typeId() != QMetaType::QVariantMap) {
            continue;
        }
        const QVariantMap src = entry.toMap();
        QVariantMap canon;
        canon[ConfigKeys::triggerModifierField()] = src.value(ConfigKeys::triggerModifierField(), 0).toInt();
        canon[ConfigKeys::triggerMouseButtonField()] = src.value(ConfigKeys::triggerMouseButtonField(), 0).toInt();
        out.append(canon);
    }
    return QVariant(out);
}

/// Canonicalize a per-algorithm settings map: round-trip through
/// @c AutotileConfig so each algorithm's settings are validated against
/// its schema and unknown keys are dropped. Idempotent:
/// @c perAlgoToVariantMap(perAlgoFromVariantMap(x)) == perAlgoToVariantMap(perAlgoFromVariantMap(it)).
QVariant sanitizePerAlgorithmSettings(const QVariant& v)
{
    return QVariant(AutotileConfig::perAlgoToVariantMap(AutotileConfig::perAlgoFromVariantMap(v.toMap())));
}
} // namespace

// ─── Shaders ────────────────────────────────────────────────────────────────
// Controls: overall effect toggle, frame rate, audio visualizer, bar count.

void appendShadersSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::shadersGroup()] = {
        {CD::enabledKey(), CD::enableShaderEffects(), QMetaType::Bool},
        {CD::frameRateKey(),
         CD::shaderFrameRate(),
         QMetaType::Int,
         {},
         clampInt(CD::shaderFrameRateMin(), CD::shaderFrameRateMax())},
        {CD::audioVisualizerKey(), CD::enableAudioVisualizer(), QMetaType::Bool},
        {CD::audioSpectrumBarCountKey(),
         CD::audioSpectrumBarCount(),
         QMetaType::Int,
         {},
         clampInt(CD::audioSpectrumBarCountMin(), CD::audioSpectrumBarCountMax())},
    };
}

// ─── Appearance ─────────────────────────────────────────────────────────────
// Five sub-groups under Snapping.Appearance.*: Colors (system toggle + 3
// zone colors), Labels (font family/color/scale/weight + italic/underline/
// strikeout toggles), Opacity (active + inactive), Border (width + radius),
// plus Effects.Blur which shares the load function.

void appendAppearanceSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    schema.groups[CD::snappingAppearanceColorsGroup()] = {
        {CD::useSystemKey(), CD::useSystemColors(), QMetaType::Bool},
        {CD::highlightKey(), CD::highlightColor(), QMetaType::QColor, {}, validColorOr(CD::highlightColor())},
        {CD::inactiveKey(), CD::inactiveColor(), QMetaType::QColor, {}, validColorOr(CD::inactiveColor())},
        {CD::borderKey(), CD::borderColor(), QMetaType::QColor, {}, validColorOr(CD::borderColor())},
    };

    schema.groups[CD::snappingAppearanceLabelsGroup()] = {
        {CD::fontColorKey(), CD::labelFontColor(), QMetaType::QColor, {}, validColorOr(CD::labelFontColor())},
        {CD::fontFamilyKey(), CD::labelFontFamily(), QMetaType::QString},
        {CD::fontSizeScaleKey(),
         CD::labelFontSizeScale(),
         QMetaType::Double,
         {},
         clampDouble(CD::labelFontSizeScaleMin(), CD::labelFontSizeScaleMax())},
        {CD::fontWeightKey(),
         CD::labelFontWeight(),
         QMetaType::Int,
         {},
         clampInt(CD::labelFontWeightMin(), CD::labelFontWeightMax())},
        {CD::fontItalicKey(), CD::labelFontItalic(), QMetaType::Bool},
        {CD::fontUnderlineKey(), CD::labelFontUnderline(), QMetaType::Bool},
        {CD::fontStrikeoutKey(), CD::labelFontStrikeout(), QMetaType::Bool},
    };

    schema.groups[CD::snappingAppearanceOpacityGroup()] = {
        {CD::activeKey(),
         CD::activeOpacity(),
         QMetaType::Double,
         {},
         clampDouble(CD::activeOpacityMin(), CD::activeOpacityMax())},
        {CD::inactiveKey(),
         CD::inactiveOpacity(),
         QMetaType::Double,
         {},
         clampDouble(CD::inactiveOpacityMin(), CD::inactiveOpacityMax())},
    };

    schema.groups[CD::snappingAppearanceBorderGroup()] = {
        {CD::widthKey(), CD::borderWidth(), QMetaType::Int, {}, clampInt(CD::borderWidthMin(), CD::borderWidthMax())},
        {CD::radiusKey(),
         CD::borderRadius(),
         QMetaType::Int,
         {},
         clampInt(CD::borderRadiusMin(), CD::borderRadiusMax())},
    };
    // Effects.Blur lives in the Effects group alongside the display-OSD keys;
    // the whole Effects group is declared in one shot by appendDisplaySchema
    // below to avoid split-across-two-call-sites ordering bugs.
}

// ─── Ordering ───────────────────────────────────────────────────────────────
// User-defined sort order for the layout picker and tiling algorithm menu.
// Both are comma-joined lists on disk; the canonicalCommaList validator
// normalizes formatting (trim, de-dupe) on every read/write.

void appendOrderingSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::orderingGroup()] = {
        {CD::snappingLayoutOrderKey(), QString(), QMetaType::QString, {}, canonicalCommaList},
        {CD::tilingAlgorithmOrderKey(), QString(), QMetaType::QString, {}, canonicalCommaList},
    };
}

// ─── Animations ─────────────────────────────────────────────────────────────
// Phase 4 sub-commit 6 — storage format is a single Profile JSON blob
// (`Animations.Profile`) replacing the pre-migration five per-field
// entries (Duration / EasingCurve / MinDistance / SequenceMode /
// StaggerInterval). Per decision S, the old keys are dead on disk; no
// migration code. Users with configs predating this schema lose their
// animation customisation and read back the library-default Profile.
//
// The per-field accessor surface on Settings (animationDuration / etc.)
// is preserved as projections over the Profile blob — see settings.cpp.
//
// Validation: the Profile JSON string is stored as-is. Clamping happens
// at the library level (Profile::effective* + AnimationController's
// clampProfile on the hot path) rather than in the schema, because the
// schema's per-field QMetaType::Int validator can't see inside the
// JSON. A malformed blob falls back to the library default via
// Profile::fromJson's permissive parse — consistent with the "garbage
// in disk → sensible defaults on read" invariant the rest of the
// v3 config uses.

void appendAnimationsSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    // Schema construction runs at Settings init time — before the daemon
    // wires its per-process CurveRegistry. Use a local static fallback
    // that auto-registers builtins (spring, cubic-bezier, elastic, bounce)
    // so the default Profile JSON resolves correctly.
    static PhosphorAnimation::CurveRegistry sSchemaRegistry;
    schema.groups[CD::animationsGroup()] = {
        {CD::enabledKey(), CD::animationsEnabled(), QMetaType::Bool},
        {CD::animationProfileKey(), CD::animationProfile(sSchemaRegistry), QMetaType::QString},
    };
}

// ─── Rendering ──────────────────────────────────────────────────────────────
// Single-key group selecting the GPU backend. The validator coerces any
// unknown string to a known value via ConfigDefaults::normalizeRenderingBackend
// so hand-edited configs can't persist garbage.

void appendRenderingSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::renderingGroup()] = {
        {CD::backendKey(),
         CD::renderingBackend(),
         QMetaType::QString,
         {},
         [](const QVariant& v) {
             return QVariant(CD::normalizeRenderingBackend(v.toString()));
         }},
    };
}

// ─── Performance ────────────────────────────────────────────────────────────
// Poll interval + minimum zone size thresholds. All clamped ints.

void appendPerformanceSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::performanceGroup()] = {
        {CD::pollIntervalMsKey(),
         CD::pollIntervalMs(),
         QMetaType::Int,
         {},
         clampInt(CD::pollIntervalMsMin(), CD::pollIntervalMsMax())},
        {CD::minimumZoneSizePxKey(),
         CD::minimumZoneSizePx(),
         QMetaType::Int,
         {},
         clampInt(CD::minimumZoneSizePxMin(), CD::minimumZoneSizePxMax())},
        {CD::minimumZoneDisplaySizePxKey(),
         CD::minimumZoneDisplaySizePx(),
         QMetaType::Int,
         {},
         clampInt(CD::minimumZoneDisplaySizePxMin(), CD::minimumZoneDisplaySizePxMax())},
    };
}

// ─── PhosphorZones::Zone Geometry (Snapping.Gaps) ──────────────────────────────────────────
// Inner/outer gaps (uniform + per-side), adjacency threshold.

void appendZoneGeometrySchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::snappingGapsGroup()] = {
        {CD::innerKey(), CD::zonePadding(), QMetaType::Int, {}, clampInt(CD::zonePaddingMin(), CD::zonePaddingMax())},
        {CD::outerKey(), CD::outerGap(), QMetaType::Int, {}, clampInt(CD::outerGapMin(), CD::outerGapMax())},
        {CD::usePerSideKey(), CD::usePerSideOuterGap(), QMetaType::Bool},
        {CD::topKey(), CD::outerGapTop(), QMetaType::Int, {}, clampInt(CD::outerGapTopMin(), CD::outerGapTopMax())},
        {CD::bottomKey(),
         CD::outerGapBottom(),
         QMetaType::Int,
         {},
         clampInt(CD::outerGapBottomMin(), CD::outerGapBottomMax())},
        {CD::leftKey(), CD::outerGapLeft(), QMetaType::Int, {}, clampInt(CD::outerGapLeftMin(), CD::outerGapLeftMax())},
        {CD::rightKey(),
         CD::outerGapRight(),
         QMetaType::Int,
         {},
         clampInt(CD::outerGapRightMin(), CD::outerGapRightMax())},
        {CD::adjacentThresholdKey(),
         CD::adjacentThreshold(),
         QMetaType::Int,
         {},
         clampInt(CD::adjacentThresholdMin(), CD::adjacentThresholdMax())},
    };
}

// ─── Shortcuts ──────────────────────────────────────────────────────────────
// Three sub-groups: Global (editor/settings launchers, zone navigation,
// snap-to-zone numbered slots, layout rotation/swap, virtual-screen rotation),
// Tiling (autotile master/ratio/count controls + retile toggle), Editor
// (zone editor shortcuts — duplicate, split, fill). All QString keys, no
// validators needed.

namespace {
// Helper: append a string KeyDef with no validator. Cuts the noise in the
// schema below when every entry is the same shape.
inline void addShortcut(QVector<PhosphorConfig::KeyDef>& list, const QString& key, const QString& defaultValue)
{
    list.append({key, defaultValue, QMetaType::QString});
}
} // namespace

void appendShortcutsSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    QVector<PhosphorConfig::KeyDef> globals;
    addShortcut(globals, CD::openEditorKey(), CD::openEditorShortcut());
    addShortcut(globals, CD::openSettingsKey(), CD::openSettingsShortcut());
    addShortcut(globals, CD::previousLayoutKey(), CD::previousLayoutShortcut());
    addShortcut(globals, CD::nextLayoutKey(), CD::nextLayoutShortcut());
    const QString quickDefaults[9] = {
        CD::quickLayout1Shortcut(), CD::quickLayout2Shortcut(), CD::quickLayout3Shortcut(),
        CD::quickLayout4Shortcut(), CD::quickLayout5Shortcut(), CD::quickLayout6Shortcut(),
        CD::quickLayout7Shortcut(), CD::quickLayout8Shortcut(), CD::quickLayout9Shortcut(),
    };
    for (int i = 0; i < 9; ++i) {
        addShortcut(globals, CD::quickLayoutKey(i + 1), quickDefaults[i]);
    }
    addShortcut(globals, CD::moveWindowLeftKey(), CD::moveWindowLeftShortcut());
    addShortcut(globals, CD::moveWindowRightKey(), CD::moveWindowRightShortcut());
    addShortcut(globals, CD::moveWindowUpKey(), CD::moveWindowUpShortcut());
    addShortcut(globals, CD::moveWindowDownKey(), CD::moveWindowDownShortcut());
    addShortcut(globals, CD::focusZoneLeftKey(), CD::focusZoneLeftShortcut());
    addShortcut(globals, CD::focusZoneRightKey(), CD::focusZoneRightShortcut());
    addShortcut(globals, CD::focusZoneUpKey(), CD::focusZoneUpShortcut());
    addShortcut(globals, CD::focusZoneDownKey(), CD::focusZoneDownShortcut());
    addShortcut(globals, CD::pushToEmptyZoneKey(), CD::pushToEmptyZoneShortcut());
    addShortcut(globals, CD::restoreWindowSizeKey(), CD::restoreWindowSizeShortcut());
    addShortcut(globals, CD::toggleWindowFloatKey(), CD::toggleWindowFloatShortcut());
    addShortcut(globals, CD::swapWindowLeftKey(), CD::swapWindowLeftShortcut());
    addShortcut(globals, CD::swapWindowRightKey(), CD::swapWindowRightShortcut());
    addShortcut(globals, CD::swapWindowUpKey(), CD::swapWindowUpShortcut());
    addShortcut(globals, CD::swapWindowDownKey(), CD::swapWindowDownShortcut());
    const QString snapToZoneDefaults[9] = {
        CD::snapToZone1Shortcut(), CD::snapToZone2Shortcut(), CD::snapToZone3Shortcut(),
        CD::snapToZone4Shortcut(), CD::snapToZone5Shortcut(), CD::snapToZone6Shortcut(),
        CD::snapToZone7Shortcut(), CD::snapToZone8Shortcut(), CD::snapToZone9Shortcut(),
    };
    for (int i = 0; i < 9; ++i) {
        addShortcut(globals, CD::snapToZoneKey(i + 1), snapToZoneDefaults[i]);
    }
    addShortcut(globals, CD::rotateWindowsClockwiseKey(), CD::rotateWindowsClockwiseShortcut());
    addShortcut(globals, CD::rotateWindowsCounterclockwiseKey(), CD::rotateWindowsCounterclockwiseShortcut());
    addShortcut(globals, CD::cycleWindowForwardKey(), CD::cycleWindowForwardShortcut());
    addShortcut(globals, CD::cycleWindowBackwardKey(), CD::cycleWindowBackwardShortcut());
    addShortcut(globals, CD::resnapToNewLayoutKey(), CD::resnapToNewLayoutShortcut());
    addShortcut(globals, CD::snapAllWindowsKey(), CD::snapAllWindowsShortcut());
    addShortcut(globals, CD::layoutPickerKey(), CD::layoutPickerShortcut());
    addShortcut(globals, CD::toggleLayoutLockKey(), CD::toggleLayoutLockShortcut());
    addShortcut(globals, CD::swapVirtualScreenLeftKey(), CD::swapVirtualScreenLeftShortcut());
    addShortcut(globals, CD::swapVirtualScreenRightKey(), CD::swapVirtualScreenRightShortcut());
    addShortcut(globals, CD::swapVirtualScreenUpKey(), CD::swapVirtualScreenUpShortcut());
    addShortcut(globals, CD::swapVirtualScreenDownKey(), CD::swapVirtualScreenDownShortcut());
    addShortcut(globals, CD::rotateVirtualScreensClockwiseKey(), CD::rotateVirtualScreensClockwiseShortcut());
    addShortcut(globals, CD::rotateVirtualScreensCounterclockwiseKey(),
                CD::rotateVirtualScreensCounterclockwiseShortcut());
    schema.groups[CD::shortcutsGlobalGroup()] = std::move(globals);

    schema.groups[CD::shortcutsTilingGroup()] = {
        {CD::toggleKey(), CD::autotileToggleShortcut(), QMetaType::QString},
        {CD::focusMasterKey(), CD::autotileFocusMasterShortcut(), QMetaType::QString},
        {CD::swapMasterKey(), CD::autotileSwapMasterShortcut(), QMetaType::QString},
        {CD::incMasterRatioKey(), CD::autotileIncMasterRatioShortcut(), QMetaType::QString},
        {CD::decMasterRatioKey(), CD::autotileDecMasterRatioShortcut(), QMetaType::QString},
        {CD::incMasterCountKey(), CD::autotileIncMasterCountShortcut(), QMetaType::QString},
        {CD::decMasterCountKey(), CD::autotileDecMasterCountShortcut(), QMetaType::QString},
        {CD::retileKey(), CD::autotileRetileShortcut(), QMetaType::QString},
    };
}

// ─── Editor ─────────────────────────────────────────────────────────────────
// Three sub-groups: Shortcuts (zone editor: duplicate/split/fill), Snapping
// (grid + edge toggles, per-axis intervals, override modifier), FillOnDrop
// (toggle + modifier). Modifier keys use a validator that rejects
// non-standard Qt::KeyboardModifier bits and falls back to the default —
// matches the hand-written load path's validModifiers mask check.

void appendEditorSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    schema.groups[CD::editorShortcutsGroup()] = {
        {CD::duplicateKey(), CD::editorDuplicateShortcut(), QMetaType::QString},
        {CD::splitHorizontalKey(), CD::editorSplitHorizontalShortcut(), QMetaType::QString},
        {CD::splitVerticalKey(), CD::editorSplitVerticalShortcut(), QMetaType::QString},
        {CD::fillKey(), CD::editorFillShortcut(), QMetaType::QString},
    };

    auto modifierOr = [](int fallback) {
        return [fallback](const QVariant& v) -> QVariant {
            const int raw = v.toInt();
            constexpr int valid = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
            if (raw == Qt::NoModifier || (raw & valid) == raw) {
                return QVariant(raw);
            }
            return QVariant(fallback);
        };
    };

    schema.groups[CD::editorSnappingGroup()] = {
        {CD::gridEnabledKey(), CD::editorGridSnappingEnabled(), QMetaType::Bool},
        {CD::edgeEnabledKey(), CD::editorEdgeSnappingEnabled(), QMetaType::Bool},
        {CD::intervalXKey(),
         CD::editorSnapIntervalX(),
         QMetaType::Double,
         {},
         clampDouble(CD::editorSnapIntervalMin(), CD::editorSnapIntervalMax())},
        {CD::intervalYKey(),
         CD::editorSnapIntervalY(),
         QMetaType::Double,
         {},
         clampDouble(CD::editorSnapIntervalMin(), CD::editorSnapIntervalMax())},
        {CD::overrideModifierKey(),
         CD::editorSnapOverrideModifier(),
         QMetaType::Int,
         {},
         modifierOr(CD::editorSnapOverrideModifier())},
    };

    schema.groups[CD::editorFillOnDropGroup()] = {
        {CD::enabledKey(), CD::fillOnDropEnabled(), QMetaType::Bool},
        {CD::modifierKey(), CD::fillOnDropModifier(), QMetaType::Int, {}, modifierOr(CD::fillOnDropModifier())},
    };
}

// ─── Exclusions ─────────────────────────────────────────────────────────────
// Apps and window classes to exclude from snapping + minimum-size filters
// + the transient-window toggle. List keys use canonicalCommaList to
// normalize formatting; ints are clamped.

void appendExclusionsSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::exclusionsGroup()] = {
        {CD::applicationsKey(), QString(), QMetaType::QString, {}, canonicalCommaList},
        {CD::windowClassesKey(), QString(), QMetaType::QString, {}, canonicalCommaList},
        {CD::transientWindowsKey(), CD::excludeTransientWindows(), QMetaType::Bool},
        {CD::minimumWindowWidthKey(),
         CD::minimumWindowWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::minimumWindowWidthMin(), CD::minimumWindowWidthMax())},
        {CD::minimumWindowHeightKey(),
         CD::minimumWindowHeight(),
         QMetaType::Int,
         {},
         clampInt(CD::minimumWindowHeightMin(), CD::minimumWindowHeightMax())},
    };
}

// ─── Display ────────────────────────────────────────────────────────────────
// Snapping.Behavior.Display plus the Effects sub-group entries that aren't
// the blur toggle (already migrated via Appearance). Enum ints (OsdStyle,
// OverlayDisplayMode) get clamp validators; lists use canonicalCommaList.
// Note: disabled-monitor connector-name resolution (Phosphor::Screens::ScreenIdentity::idForName)
// stays PZ-side — we keep the wire format as comma-joined and let the
// Settings getter do the resolution step.

void appendDisplaySchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    schema.groups[CD::snappingBehaviorDisplayGroup()] = {
        {CD::showOnAllMonitorsKey(), CD::showOnAllMonitors(), QMetaType::Bool},
        {CD::disabledMonitorsKey(), QString(), QMetaType::QString, {}, canonicalCommaList},
        {CD::disabledDesktopsKey(), QString(), QMetaType::QString, {}, canonicalCommaList},
        {CD::disabledActivitiesKey(), QString(), QMetaType::QString, {}, canonicalCommaList},
        {CD::filterByAspectRatioKey(), CD::filterLayoutsByAspectRatio(), QMetaType::Bool},
    };

    // Full Effects group declared here in one shot. Blur is logically an
    // appearance-level setting but shares the Effects JSON container with
    // the display-OSD keys below; declaring the whole container from one
    // call site keeps the schema build order-independent.
    schema.groups[CD::snappingEffectsGroup()] = {
        {CD::blurKey(), CD::enableBlur(), QMetaType::Bool},
        {CD::showNumbersKey(), CD::showNumbers(), QMetaType::Bool},
        {CD::flashOnSwitchKey(), CD::flashOnSwitch(), QMetaType::Bool},
        {CD::osdOnLayoutSwitchKey(), CD::showOsdOnLayoutSwitch(), QMetaType::Bool},
        {CD::navigationOsdKey(), CD::showNavigationOsd(), QMetaType::Bool},
        {CD::osdStyleKey(), CD::osdStyle(), QMetaType::Int, {}, clampInt(CD::osdStyleMin(), CD::osdStyleMax())},
        {CD::overlayDisplayModeKey(),
         CD::overlayDisplayMode(),
         QMetaType::Int,
         {},
         clampInt(CD::overlayDisplayModeMin(), CD::overlayDisplayModeMax())},
    };
}

// ─── PhosphorZones::Zone Selector ──────────────────────────────────────────────────────────
// Pops up at the edge of the screen during drag to let users pick which zone
// to snap to. Toggle + trigger distance + preview geometry + grid config +
// three enum-ints (Position, LayoutMode, SizeMode).

void appendZoneSelectorSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::snappingZoneSelectorGroup()] = {
        {CD::enabledKey(), CD::zoneSelectorEnabled(), QMetaType::Bool},
        {CD::triggerDistanceKey(),
         CD::triggerDistance(),
         QMetaType::Int,
         {},
         clampInt(CD::triggerDistanceMin(), CD::triggerDistanceMax())},
        {CD::positionKey(), CD::position(), QMetaType::Int, {}, clampInt(0, 8)},
        {CD::layoutModeKey(), CD::layoutMode(), QMetaType::Int, {}, clampInt(0, 2)},
        {CD::previewWidthKey(),
         CD::previewWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::previewWidthMin(), CD::previewWidthMax())},
        {CD::previewHeightKey(),
         CD::previewHeight(),
         QMetaType::Int,
         {},
         clampInt(CD::previewHeightMin(), CD::previewHeightMax())},
        {CD::previewLockAspectKey(), CD::previewLockAspect(), QMetaType::Bool},
        {CD::gridColumnsKey(),
         CD::gridColumns(),
         QMetaType::Int,
         {},
         clampInt(CD::gridColumnsMin(), CD::gridColumnsMax())},
        {CD::sizeModeKey(), CD::sizeMode(), QMetaType::Int, {}, clampInt(0, 2)},
        {CD::maxRowsKey(), CD::maxRows(), QMetaType::Int, {}, clampInt(CD::maxRowsMin(), CD::maxRowsMax())},
    };
}

// ─── Activation ─────────────────────────────────────────────────────────────
// Top-level Snapping.Enabled + the Snapping.Behavior scalar keys (drag
// activation triggers, toggle-activation). Trigger lists are stored as JSON
// strings; the backend auto-round-trips them as native JSON arrays.

void appendActivationSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::snappingGroup()] = {
        {CD::enabledKey(), CD::snappingEnabled(), QMetaType::Bool},
    };
    // Snapping.Behavior owns two scalar keys directly (Triggers, ToggleActivation);
    // the SnapAssist / ZoneSpan / WindowHandling / Display / AutotileDragInsert
    // sub-groups each get their own Schema entry below (or already migrated).
    schema.groups[CD::snappingBehaviorGroup()] = {
        {CD::triggersKey(), CD::dragActivationTriggers(), QMetaType::QVariantList, {}, canonicalTriggerList},
        {CD::toggleActivationKey(), CD::toggleActivation(), QMetaType::Bool},
    };
    schema.groups[CD::snappingBehaviorZoneSpanGroup()] = {
        {CD::enabledKey(), CD::zoneSpanEnabled(), QMetaType::Bool},
        {CD::modifierKey(),
         CD::zoneSpanModifier(),
         QMetaType::Int,
         {},
         // DragModifier int values are contiguous but not ordered — each is a
         // discrete named modifier combo. clampInt on an unknown value would
         // reinterpret e.g. 99 as the highest valid enum (CtrlAltMeta),
         // silently giving the user a much stricter capture rule than they
         // asked for. Snap-to-default (Disabled = 0) instead so upgrade-
         // mismatches or hand-edited configs fall back to "off" rather than
         // a semantically-unrelated neighbour.
         validIntOr({static_cast<int>(DragModifier::Disabled), static_cast<int>(DragModifier::Shift),
                     static_cast<int>(DragModifier::Ctrl), static_cast<int>(DragModifier::Alt),
                     static_cast<int>(DragModifier::Meta), static_cast<int>(DragModifier::CtrlAlt),
                     static_cast<int>(DragModifier::CtrlShift), static_cast<int>(DragModifier::AltShift),
                     static_cast<int>(DragModifier::AlwaysActive), static_cast<int>(DragModifier::AltMeta),
                     static_cast<int>(DragModifier::CtrlAltMeta)},
                    static_cast<int>(DragModifier::Disabled))},
        {CD::triggersKey(), CD::zoneSpanTriggers(), QMetaType::QVariantList, {}, canonicalTriggerList},
    };
}

// ─── Behavior ───────────────────────────────────────────────────────────────
// WindowHandling + SnapAssist sub-groups. The Autotile drag-insert triggers
// live in Tiling.Behavior and get scheduled under the Autotiling migration.

void appendBehaviorSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::snappingBehaviorWindowHandlingGroup()] = {
        {CD::keepOnResolutionChangeKey(), CD::keepWindowsInZonesOnResolutionChange(), QMetaType::Bool},
        {CD::moveNewToLastZoneKey(), CD::moveNewWindowsToLastZone(), QMetaType::Bool},
        {CD::restoreOnUnsnapKey(), CD::restoreOriginalSizeOnUnsnap(), QMetaType::Bool},
        {CD::stickyWindowHandlingKey(),
         CD::stickyWindowHandling(),
         QMetaType::Int,
         {},
         clampInt(static_cast<int>(StickyWindowHandling::TreatAsNormal),
                  static_cast<int>(StickyWindowHandling::IgnoreAll))},
        {CD::restoreOnLoginKey(), CD::restoreWindowsToZonesOnLogin(), QMetaType::Bool},
        {CD::defaultLayoutIdKey(), QString(), QMetaType::QString},
    };
    schema.groups[CD::snappingBehaviorSnapAssistGroup()] = {
        {CD::featureEnabledKey(), CD::snapAssistFeatureEnabled(), QMetaType::Bool},
        {CD::enabledKey(), CD::snapAssistEnabled(), QMetaType::Bool},
        {CD::triggersKey(), CD::snapAssistTriggers(), QMetaType::QVariantList, {}, canonicalTriggerList},
    };
}

// ─── Autotiling ─────────────────────────────────────────────────────────────
// The biggest group — Tiling.* has six sub-groups: Algorithm, Behavior,
// Appearance.{Colors,Decorations,Borders}, Gaps. Plus the top-level
// Tiling.Enabled toggle. PerAlgorithmSettings is a JSON-encoded QVariantMap;
// LockedScreens is a comma list; DragInsert triggers are a JSON list.

void appendAutotilingSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    schema.groups[CD::tilingGroup()] = {
        {CD::enabledKey(), CD::autotileEnabled(), QMetaType::Bool},
    };

    schema.groups[CD::tilingAlgorithmGroup()] = {
        {CD::defaultKey(), CD::defaultAutotileAlgorithm(), QMetaType::QString},
        {CD::splitRatioKey(),
         CD::autotileSplitRatio(),
         QMetaType::Double,
         {},
         clampDouble(CD::autotileSplitRatioMin(), CD::autotileSplitRatioMax())},
        {CD::splitRatioStepKey(),
         CD::autotileSplitRatioStep(),
         QMetaType::Double,
         {},
         clampDouble(CD::autotileSplitRatioStepMin(), CD::autotileSplitRatioStepMax())},
        {CD::masterCountKey(),
         CD::autotileMasterCount(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileMasterCountMin(), CD::autotileMasterCountMax())},
        {CD::maxWindowsKey(),
         CD::autotileMaxWindows(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileMaxWindowsMin(), CD::autotileMaxWindowsMax())},
        {CD::perAlgorithmSettingsKey(), QVariantMap{}, QMetaType::QVariantMap, {}, sanitizePerAlgorithmSettings},
    };

    schema.groups[CD::tilingBehaviorGroup()] = {
        {CD::insertPositionKey(),
         CD::autotileInsertPosition(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileInsertPositionMin(), CD::autotileInsertPositionMax())},
        {CD::focusNewWindowsKey(), CD::autotileFocusNewWindows(), QMetaType::Bool},
        {CD::focusFollowsMouseKey(), CD::autotileFocusFollowsMouse(), QMetaType::Bool},
        {CD::respectMinimumSizeKey(), CD::autotileRespectMinimumSize(), QMetaType::Bool},
        {CD::stickyWindowHandlingKey(),
         CD::autotileStickyWindowHandling(),
         QMetaType::Int,
         {},
         clampInt(static_cast<int>(StickyWindowHandling::TreatAsNormal),
                  static_cast<int>(StickyWindowHandling::IgnoreAll))},
        {CD::dragBehaviorKey(),
         CD::autotileDragBehavior(),
         QMetaType::Int,
         {},
         validIntOr({static_cast<int>(AutotileDragBehavior::Float), static_cast<int>(AutotileDragBehavior::Reorder)},
                    static_cast<int>(AutotileDragBehavior::Float))},
        {CD::overflowBehaviorKey(),
         CD::autotileOverflowBehavior(),
         QMetaType::Int,
         {},
         validIntOr(
             {static_cast<int>(AutotileOverflowBehavior::Float), static_cast<int>(AutotileOverflowBehavior::Unlimited)},
             static_cast<int>(AutotileOverflowBehavior::Float))},
        {CD::lockedScreensKey(), QString(), QMetaType::QString, {}, canonicalCommaList},
        {CD::triggersKey(), CD::autotileDragInsertTriggers(), QMetaType::QVariantList, {}, canonicalTriggerList},
        {CD::toggleActivationKey(), CD::autotileDragInsertToggle(), QMetaType::Bool},
    };

    schema.groups[CD::tilingAppearanceColorsGroup()] = {
        {CD::activeKey(), CD::autotileBorderColor(), QMetaType::QColor, {}, validColorOr(CD::autotileBorderColor())},
        {CD::inactiveKey(),
         CD::autotileInactiveBorderColor(),
         QMetaType::QColor,
         {},
         validColorOr(CD::autotileInactiveBorderColor())},
        {CD::useSystemKey(), CD::autotileUseSystemBorderColors(), QMetaType::Bool},
    };

    schema.groups[CD::tilingAppearanceDecorationsGroup()] = {
        {CD::hideTitleBarsKey(), CD::autotileHideTitleBars(), QMetaType::Bool},
    };

    schema.groups[CD::tilingAppearanceBordersGroup()] = {
        {CD::showBorderKey(), CD::autotileShowBorder(), QMetaType::Bool},
        {CD::widthKey(),
         CD::autotileBorderWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileBorderWidthMin(), CD::autotileBorderWidthMax())},
        {CD::radiusKey(),
         CD::autotileBorderRadius(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileBorderRadiusMin(), CD::autotileBorderRadiusMax())},
    };

    schema.groups[CD::tilingGapsGroup()] = {
        {CD::innerKey(),
         CD::autotileInnerGap(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileInnerGapMin(), CD::autotileInnerGapMax())},
        {CD::outerKey(),
         CD::autotileOuterGap(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileOuterGapMin(), CD::autotileOuterGapMax())},
        {CD::usePerSideKey(), CD::autotileUsePerSideOuterGap(), QMetaType::Bool},
        {CD::topKey(),
         CD::autotileOuterGapTop(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileOuterGapTopMin(), CD::autotileOuterGapTopMax())},
        {CD::bottomKey(),
         CD::autotileOuterGapBottom(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileOuterGapBottomMin(), CD::autotileOuterGapBottomMax())},
        {CD::leftKey(),
         CD::autotileOuterGapLeft(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileOuterGapLeftMin(), CD::autotileOuterGapLeftMax())},
        {CD::rightKey(),
         CD::autotileOuterGapRight(),
         QMetaType::Int,
         {},
         clampInt(CD::autotileOuterGapRightMin(), CD::autotileOuterGapRightMax())},
        {CD::smartGapsKey(), CD::autotileSmartGaps(), QMetaType::Bool},
    };
}

} // namespace PlasmaZones
