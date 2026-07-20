// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsschema.h"

#include "settingsschemachoices.h"

#include "configdefaults.h"
#include "configmigration.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include "../core/enums.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <QtGlobal>
#include <PhosphorScreens/ScreenIdentity.h>

using namespace Qt::StringLiterals;

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
    appendCheatsheetSchema(s);
    appendEditorSchema(s);
    appendExclusionsSchema(s);
    appendDisplaySchema(s);
    appendZoneSelectorSchema(s);
    appendActivationSchema(s);
    appendBehaviorSchema(s);
    appendAutotilingSchema(s);
    appendWindowsSchema(s);
    appendGapsSchema(s);
    appendDecorationsSchema(s);

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

/// Snap-to-default string-enum validator: the closed-set string analogue of
/// validIntOr. Accept the value only if it is one of @p valid, otherwise return
/// @p fallback. Used for closed-set tokens (e.g. the appearance "Apply to" scope)
/// so a hand-edited garbage token in the on-disk file can't flow to the effect.
auto validStringOr(std::initializer_list<QLatin1String> valid, QString fallback)
{
    return [valid = QVector<QLatin1String>(valid), fallback = std::move(fallback)](const QVariant& v) -> QVariant {
        const QString raw = v.toString();
        for (const QLatin1String& tok : valid) {
            if (raw == tok) {
                return raw;
            }
        }
        return fallback;
    };
}

/// Window-border colour validator. The value is a string that is EITHER the
/// "accent" sentinel (which the effect resolves to the live system colour) OR any
/// string QColor accepts (a #hex or a named colour, matching what the effect
/// parses); snap anything else to @p fallback. Kept as a string round-trip
/// (not validColorOr, which coerces to QColor and would drop the sentinel) so a
/// hand-edited garbage colour in the on-disk file can't flow to the effect. The
/// bare "accent" literal mirrors ConfigDefaults::windowBorderColorActive (the
/// config layer deliberately avoids pulling PhosphorRules::BorderColorToken in).
auto validBorderColorOr(QString fallback)
{
    return [fallback = std::move(fallback)](const QVariant& v) -> QVariant {
        const QString raw = v.toString();
        if (raw == QLatin1String("accent") || QColor(raw).isValid()) {
            return raw;
        }
        return fallback;
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
    return QVariant(PhosphorTileEngine::AutotileConfig::perAlgoToVariantMap(
        PhosphorTileEngine::AutotileConfig::perAlgoFromVariantMap(v.toMap())));
}
} // namespace

// ─── Shaders ────────────────────────────────────────────────────────────────
// Controls: frame rate, plus the full audio-spectrum parameter set in the
// Shaders.Audio sub-group. String choices are coerced by the ConfigDefaults
// normalizers so hand-edited configs can't persist garbage; the input source
// is free-form (the provider sanitizes it before it reaches cava).

void appendShadersSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::shadersGroup()] = {
        {CD::frameRateKey(),
         CD::shaderFrameRate(),
         QMetaType::Int,
         {},
         clampInt(CD::shaderFrameRateMin(), CD::shaderFrameRateMax())},
    };
    schema.groups[CD::shadersAudioGroup()] = {
        {CD::enabledKey(), CD::enableAudioVisualizer(), QMetaType::Bool},
        {CD::barsKey(),
         CD::audioSpectrumBarCount(),
         QMetaType::Int,
         {},
         clampInt(CD::audioSpectrumBarCountMin(), CD::audioSpectrumBarCountMax())},
        {CD::autosensKey(), CD::audioAutosens(), QMetaType::Bool},
        {CD::sensitivityKey(),
         CD::audioSensitivity(),
         QMetaType::Int,
         {},
         clampInt(CD::audioSensitivityMin(), CD::audioSensitivityMax())},
        {CD::noiseReductionKey(),
         CD::audioNoiseReduction(),
         QMetaType::Int,
         {},
         clampInt(CD::audioNoiseReductionMin(), CD::audioNoiseReductionMax())},
        {CD::lowerCutoffHzKey(),
         CD::audioLowerCutoffHz(),
         QMetaType::Int,
         {},
         clampInt(CD::audioLowerCutoffHzMin(), CD::audioLowerCutoffHzMax())},
        {CD::higherCutoffHzKey(),
         CD::audioHigherCutoffHz(),
         QMetaType::Int,
         {},
         clampInt(CD::audioHigherCutoffHzMin(), CD::audioHigherCutoffHzMax())},
        {CD::monstercatKey(), CD::audioMonstercat(), QMetaType::Bool},
        {CD::wavesKey(), CD::audioWaves(), QMetaType::Bool},
        {CD::channelModeKey(),
         CD::audioChannelMode(),
         QMetaType::QString,
         {},
         [](const QVariant& v) {
             return QVariant(CD::normalizeAudioChannelMode(v.toString()));
         },
         tokenChoices(CD::audioChannelModeOptions())},
        {CD::reverseKey(), CD::audioReverse(), QMetaType::Bool},
        {CD::extraSmoothingKey(),
         CD::audioExtraSmoothing(),
         QMetaType::Int,
         {},
         clampInt(CD::audioExtraSmoothingMin(), CD::audioExtraSmoothingMax())},
        {CD::inputMethodKey(),
         CD::audioInputMethod(),
         QMetaType::QString,
         {},
         [](const QVariant& v) {
             return QVariant(CD::normalizeAudioInputMethod(v.toString()));
         },
         tokenChoices(CD::audioInputMethodOptions())},
        {CD::inputSourceKey(), CD::audioInputSource(), QMetaType::QString},
    };
}

// ─── Appearance ─────────────────────────────────────────────────────────────
// Declares four zone-overlay sub-groups under Snapping.Zones.*: Colors (system
// toggle + 3 zone colors), Labels (font family/color/scale/weight + italic/
// underline/strikeout toggles), Opacity (active + inactive), Border (width +
// radius). The per-mode snapped-window
// decoration groups that used to live here are gone — window border and title-bar
// appearance moved to the top-level mode-neutral Windows config group (see
// appendWindowsSchema).

void appendAppearanceSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    schema.groups[CD::snappingZonesColorsGroup()] = {
        {CD::useSystemKey(), CD::useSystemColors(), QMetaType::Bool},
        {CD::highlightKey(), CD::highlightColor(), QMetaType::QColor, {}, validColorOr(CD::highlightColor())},
        {CD::inactiveKey(), CD::inactiveColor(), QMetaType::QColor, {}, validColorOr(CD::inactiveColor())},
        {CD::borderKey(), CD::borderColor(), QMetaType::QColor, {}, validColorOr(CD::borderColor())},
    };

    schema.groups[CD::snappingZonesLabelsGroup()] = {
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

    schema.groups[CD::snappingZonesOpacityGroup()] = {
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

    schema.groups[CD::snappingZonesBorderGroup()] = {
        {CD::widthKey(), CD::borderWidth(), QMetaType::Int, {}, clampInt(CD::borderWidthMin(), CD::borderWidthMax())},
        {CD::radiusKey(),
         CD::borderRadius(),
         QMetaType::Int,
         {},
         clampInt(CD::borderRadiusMin(), CD::borderRadiusMax())},
    };
    // The Effects group (display-OSD keys) is declared in one shot by
    // appendDisplaySchema below to avoid split-across-two-call-sites
    // ordering bugs.
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
    // Default-Profile JSON resolution needs a CurveRegistry. The daemon's
    // per-process registry is wired at startup (`daemon/main.cpp`), but
    // standalone settings / unit tests construct Settings without injecting
    // one — for those paths we fall back to this function-local static that
    // auto-registers the builtins (spring, cubic-bezier, elastic, bounce).
    // The static persists across Settings re-construction.
    static PhosphorAnimation::CurveRegistry sSchemaRegistry;
    schema.groups[CD::animationsGroup()] = {
        {CD::enabledKey(), CD::animationsEnabled(), QMetaType::Bool},
        // Profile and ShaderProfileTree persist as nested JSON objects
        // (QVariantMap) so the on-disk config shows their structure
        // directly. Existing string-blob configs are migrated transparently
        // by Store::read's legacy-string fallback on first load.
        {CD::animationProfileKey(), CD::animationProfile(sSchemaRegistry), QMetaType::QVariantMap},
        {CD::shaderProfileTreeKey(), CD::shaderProfileTree(), QMetaType::QVariantMap},
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
         },
         tokenChoices(CD::renderingBackendOptions())},
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
    // The shared inner/outer gaps live in the top-level Gaps group
    // (appendGapsSchema); the Window Appearance page edits them as plain config.
    // Snapping.Gaps keeps only the snapping-specific adjacency threshold.
    schema.groups[CD::snappingGapsGroup()] = {
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
    addShortcut(globals, CD::toggleCheatsheetKey(), CD::toggleCheatsheetShortcut());
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

// ─── Cheatsheet ─────────────────────────────────────────────────────────────
// The shortcut cheatsheet overlay's own group (the toggle shortcut string
// lives in Shortcuts.Global with its siblings, registered above).

void appendCheatsheetSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::cheatsheetGroup()] = {
        {CD::enabledKey(), CD::cheatsheetEnabled(), QMetaType::Bool},
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
        // Qt::KeyboardModifier BITMASK, not the DragModifier enum that the
        // identically named zone-span key uses — there is no closed choice
        // set to declare here.
        {CD::modifierKey(), CD::fillOnDropModifier(), QMetaType::Int, {}, modifierOr(CD::fillOnDropModifier())},
    };
}

// ─── Exclusions + Animation + Decoration Window Filtering ───────────────────
// Three distinct schema groups declared together:
//   1. `Exclusions` — snapping/tiling minimum-size + transient-window
//      globals.
//   2. `Animations.WindowFiltering` — animation-side equivalents plus a
//      NotificationsAndOsd knob.
//   3. `Decorations.WindowFiltering` — border/decoration-side equivalents.
// The first two retired their per-app / per-class string lists in v4 (folded
// into Application-subject Rules); only the global behavioural knobs survive.
// Ints are clamped via schema validators.

void appendExclusionsSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::exclusionsGroup()] = {
        // The `Applications` / `WindowClasses` leaf keys retired in v4 —
        // the v4 migration drains them into Application-subject Exclude
        // Rules. Re-declaring them here would let the schema-driven
        // backend silently re-write dead defaults under the Exclusions
        // group, re-introducing keys we explicitly migrated out in v3→v4.
        // Only the three global knobs survive in this group.
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

    // Animation window filtering — same shape as the Exclusions group
    // above plus a NotificationsAndOsd knob, stored independently so a
    // user can disable animations for an app while still snapping it
    // (or vice versa).
    schema.groups[CD::animationsWindowFilteringGroup()] = {
        // The `Applications` / `WindowClasses` leaf keys retired in v4 —
        // the v4 migration drains them into ExcludeAnimations Rules.
        // Only the four global knobs survive in this group.
        {CD::transientWindowsKey(), CD::animationExcludeTransientWindows(), QMetaType::Bool},
        {CD::notificationsAndOsdKey(), CD::animationExcludeNotificationsAndOsd(), QMetaType::Bool},
        {CD::minimumWindowWidthKey(),
         CD::animationMinimumWindowWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::animationMinimumWindowWidthMin(), CD::animationMinimumWindowWidthMax())},
        {CD::minimumWindowHeightKey(),
         CD::animationMinimumWindowHeight(),
         QMetaType::Int,
         {},
         clampInt(CD::animationMinimumWindowHeightMin(), CD::animationMinimumWindowHeightMax())},
    };

    // Decoration window filtering — same shape as the Exclusions group,
    // stored independently so the KWin effect's border pass can be tuned
    // separately from snapping and animation filtering. Reuses the shared
    // leaf keys; only the group differs.
    schema.groups[CD::decorationsWindowFilteringGroup()] = {
        {CD::transientWindowsKey(), CD::decorationExcludeTransientWindows(), QMetaType::Bool},
        {CD::minimumWindowWidthKey(),
         CD::decorationMinimumWindowWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::decorationMinimumWindowWidthMin(), CD::decorationMinimumWindowWidthMax())},
        {CD::minimumWindowHeightKey(),
         CD::decorationMinimumWindowHeight(),
         QMetaType::Int,
         {},
         clampInt(CD::decorationMinimumWindowHeightMin(), CD::decorationMinimumWindowHeightMax())},
    };
}

// ─── Display ────────────────────────────────────────────────────────────────
// Snapping.Behavior.Display plus the Effects sub-group. Enum ints (OsdStyle,
// OverlayDisplayMode) get clamp validators; lists use canonicalCommaList.
//
// Per-mode disable lists (formerly Display.{Snapping,Autotile}Disabled*)
// are NOT in the schema: as of the window-rule refactor (PR #477) every
// read/write routes through `rules.json` via Settings::disableEntriesFor
// / writeDisableEntries. Re-declaring them here would let the schema-driven
// backend silently re-write dead defaults under the mode-neutral Display
// group, re-introducing keys we explicitly migrated out in v3→v4. The v3
// key accessors live on solely for migrateV3ToV4 to read the legacy values
// before they are moved to the rule store.

void appendDisplaySchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    schema.groups[CD::snappingBehaviorDisplayGroup()] = {
        {CD::showOnAllMonitorsKey(), CD::showOnAllMonitors(), QMetaType::Bool},
        {CD::filterByAspectRatioKey(), CD::filterLayoutsByAspectRatio(), QMetaType::Bool},
    };

    // Full Effects group declared here in one shot; declaring the whole
    // container from one call site keeps the schema build order-independent.
    // The retired blur toggle ("Blur") is intentionally NOT declared:
    // purgeStaleKeys() evicts the leftover key from existing user configs
    // on the next save().
    schema.groups[CD::snappingEffectsGroup()] = {
        {CD::showNumbersKey(), CD::showNumbers(), QMetaType::Bool},
        {CD::flashOnSwitchKey(), CD::flashOnSwitch(), QMetaType::Bool},
        {CD::osdOnLayoutSwitchKey(), CD::showOsdOnLayoutSwitch(), QMetaType::Bool},
        {CD::osdOnDesktopSwitchKey(), CD::showOsdOnDesktopSwitch(), QMetaType::Bool},
        {CD::navigationOsdKey(), CD::showNavigationOsd(), QMetaType::Bool},
        {CD::osdStyleKey(),
         CD::osdStyle(),
         QMetaType::Int,
         {},
         clampInt(CD::osdStyleMin(), CD::osdStyleMax()),
         intChoices({{static_cast<int>(OsdStyle::None), "none"_L1},
                     {static_cast<int>(OsdStyle::Text), "text"_L1},
                     {static_cast<int>(OsdStyle::Preview), "preview"_L1}})},
        {CD::overlayDisplayModeKey(),
         CD::overlayDisplayMode(),
         QMetaType::Int,
         {},
         clampInt(CD::overlayDisplayModeMin(), CD::overlayDisplayModeMax()),
         intChoices({{static_cast<int>(OverlayDisplayMode::ZoneRectangles), "zoneRectangles"_L1},
                     {static_cast<int>(OverlayDisplayMode::LayoutPreview), "layoutPreview"_L1}})},
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
        {CD::positionKey(),
         CD::position(),
         QMetaType::Int,
         {},
         clampInt(static_cast<int>(ZoneSelectorPosition::TopLeft), static_cast<int>(ZoneSelectorPosition::BottomRight)),
         intChoices({{static_cast<int>(ZoneSelectorPosition::TopLeft), "topLeft"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Top), "top"_L1},
                     {static_cast<int>(ZoneSelectorPosition::TopRight), "topRight"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Left), "left"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Center), "center"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Right), "right"_L1},
                     {static_cast<int>(ZoneSelectorPosition::BottomLeft), "bottomLeft"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Bottom), "bottom"_L1},
                     {static_cast<int>(ZoneSelectorPosition::BottomRight), "bottomRight"_L1}})},
        {CD::layoutModeKey(),
         CD::layoutMode(),
         QMetaType::Int,
         {},
         clampInt(static_cast<int>(ZoneSelectorLayoutMode::Grid), static_cast<int>(ZoneSelectorLayoutMode::Vertical)),
         intChoices({{static_cast<int>(ZoneSelectorLayoutMode::Grid), "grid"_L1},
                     {static_cast<int>(ZoneSelectorLayoutMode::Horizontal), "horizontal"_L1},
                     {static_cast<int>(ZoneSelectorLayoutMode::Vertical), "vertical"_L1}})},
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
        {CD::sizeModeKey(),
         CD::sizeMode(),
         QMetaType::Int,
         {},
         clampInt(static_cast<int>(ZoneSelectorSizeMode::Auto), static_cast<int>(ZoneSelectorSizeMode::Manual)),
         intChoices({{static_cast<int>(ZoneSelectorSizeMode::Auto), "auto"_L1},
                     {static_cast<int>(ZoneSelectorSizeMode::Manual), "manual"_L1}})},
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
        {CD::focusNewWindowsKey(), CD::snappingFocusNewWindows(), QMetaType::Bool},
        {CD::focusFollowsMouseKey(), CD::snappingFocusFollowsMouse(), QMetaType::Bool},
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
                    static_cast<int>(DragModifier::Disabled)),
         intChoices({{static_cast<int>(DragModifier::Disabled), "disabled"_L1},
                     {static_cast<int>(DragModifier::Shift), "shift"_L1},
                     {static_cast<int>(DragModifier::Ctrl), "ctrl"_L1},
                     {static_cast<int>(DragModifier::Alt), "alt"_L1},
                     {static_cast<int>(DragModifier::Meta), "meta"_L1},
                     {static_cast<int>(DragModifier::CtrlAlt), "ctrlAlt"_L1},
                     {static_cast<int>(DragModifier::CtrlShift), "ctrlShift"_L1},
                     {static_cast<int>(DragModifier::AltShift), "altShift"_L1},
                     {static_cast<int>(DragModifier::AlwaysActive), "alwaysActive"_L1},
                     {static_cast<int>(DragModifier::AltMeta), "altMeta"_L1},
                     {static_cast<int>(DragModifier::CtrlAltMeta), "ctrlAltMeta"_L1}})},
        {CD::triggersKey(), CD::zoneSpanTriggers(), QMetaType::QVariantList, {}, canonicalTriggerList},
        {CD::toggleActivationKey(), CD::zoneSpanToggleMode(), QMetaType::Bool},
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
         CD::snappingStickyWindowHandling(),
         QMetaType::Int,
         {},
         clampInt(static_cast<int>(StickyWindowHandling::TreatAsNormal),
                  static_cast<int>(StickyWindowHandling::IgnoreAll)),
         intChoices({{static_cast<int>(StickyWindowHandling::TreatAsNormal), "treatAsNormal"_L1},
                     {static_cast<int>(StickyWindowHandling::RestoreOnly), "restoreOnly"_L1},
                     {static_cast<int>(StickyWindowHandling::IgnoreAll), "ignoreAll"_L1}})},
        {CD::restoreOnLoginKey(), CD::restoreWindowsToZonesOnLogin(), QMetaType::Bool},
        {CD::restoreFloatedOnLoginKey(), CD::snappingRestoreFloatedWindowsOnLogin(), QMetaType::Bool},
        {CD::unfloatFallbackToZoneKey(), CD::snapUnfloatFallbackToZone(), QMetaType::Bool},
        {CD::autoAssignAllLayoutsKey(), CD::autoAssignAllLayouts(), QMetaType::Bool},
        {CD::suppressDefaultLayoutAssignmentKey(), CD::suppressDefaultLayoutAssignment(), QMetaType::Bool},
        {CD::defaultLayoutIdKey(), QString(), QMetaType::QString},
    };
    schema.groups[CD::snappingBehaviorSnapAssistGroup()] = {
        {CD::featureEnabledKey(), CD::snapAssistFeatureEnabled(), QMetaType::Bool},
        {CD::enabledKey(), CD::snapAssistEnabled(), QMetaType::Bool},
        {CD::triggersKey(), CD::snapAssistTriggers(), QMetaType::QVariantList, {}, canonicalTriggerList},
    };
}

// ─── Autotiling ─────────────────────────────────────────────────────────────
// Tiling.* has three sub-groups: Algorithm, Behavior, Gaps. Plus the top-level
// Tiling.Enabled toggle. (The Appearance.{Colors,Decorations,Borders} groups
// that used to live here are gone — window border and title-bar appearance moved
// to the top-level mode-neutral Windows config group, see appendWindowsSchema.)
// PerAlgorithmSettings is a JSON-encoded QVariantMap;
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
         clampInt(CD::autotileInsertPositionMin(), CD::autotileInsertPositionMax()),
         intChoices({{static_cast<int>(AutotileInsertPosition::End), "end"_L1},
                     {static_cast<int>(AutotileInsertPosition::AfterFocused), "afterFocused"_L1},
                     {static_cast<int>(AutotileInsertPosition::AsMaster), "asMaster"_L1}})},
        {CD::focusNewWindowsKey(), CD::autotileFocusNewWindows(), QMetaType::Bool},
        {CD::focusFollowsMouseKey(), CD::autotileFocusFollowsMouse(), QMetaType::Bool},
        {CD::respectMinimumSizeKey(), CD::autotileRespectMinimumSize(), QMetaType::Bool},
        {CD::restoreFloatedOnLoginKey(), CD::autotileRestoreFloatedWindowsOnLogin(), QMetaType::Bool},
        {CD::stickyWindowHandlingKey(),
         CD::autotileStickyWindowHandling(),
         QMetaType::Int,
         {},
         clampInt(static_cast<int>(StickyWindowHandling::TreatAsNormal),
                  static_cast<int>(StickyWindowHandling::IgnoreAll)),
         intChoices({{static_cast<int>(StickyWindowHandling::TreatAsNormal), "treatAsNormal"_L1},
                     {static_cast<int>(StickyWindowHandling::RestoreOnly), "restoreOnly"_L1},
                     {static_cast<int>(StickyWindowHandling::IgnoreAll), "ignoreAll"_L1}})},
        {CD::dragBehaviorKey(),
         CD::autotileDragBehavior(),
         QMetaType::Int,
         {},
         validIntOr({static_cast<int>(AutotileDragBehavior::Float), static_cast<int>(AutotileDragBehavior::Reorder)},
                    static_cast<int>(AutotileDragBehavior::Float)),
         intChoices({{static_cast<int>(AutotileDragBehavior::Float), "float"_L1},
                     {static_cast<int>(AutotileDragBehavior::Reorder), "reorder"_L1}})},
        {CD::overflowBehaviorKey(),
         CD::autotileOverflowBehavior(),
         QMetaType::Int,
         {},
         validIntOr(
             {static_cast<int>(AutotileOverflowBehavior::Float), static_cast<int>(AutotileOverflowBehavior::Unlimited)},
             static_cast<int>(AutotileOverflowBehavior::Float)),
         intChoices({{static_cast<int>(AutotileOverflowBehavior::Float), "float"_L1},
                     {static_cast<int>(AutotileOverflowBehavior::Unlimited), "unlimited"_L1}})},
        {CD::lockedScreensKey(), QString(), QMetaType::QString, {}, canonicalCommaList},
        {CD::triggersKey(), CD::autotileDragInsertTriggers(), QMetaType::QVariantList, {}, canonicalTriggerList},
        {CD::toggleActivationKey(), CD::autotileDragInsertToggle(), QMetaType::Bool},
    };

    // Tiling.Gaps keeps only the tiling-specific SmartGaps toggle. The shared
    // inner/outer gaps live in the top-level Gaps group (appendGapsSchema).
    schema.groups[CD::tilingGapsGroup()] = {
        {CD::smartGapsKey(), CD::autotileSmartGaps(), QMetaType::Bool},
    };
}

// ─── Windows (window decoration appearance) ─────────────────────────────────
// Mode-neutral window border + title bar. Border colours are strings (the
// "accent" sentinel, or a hex/named colour) validated by the string-form
// validBorderColorOr rather than the QColor-coercing validColorOr. The
// border/title-bar scope is a closed-set token ("tiled" / "normal" / "all")
// the Appearance page and the effect agree on, snapped to the default on an
// unknown on-disk value.
// Width/radius are clamped ints reusing the generic Width/Radius keys (the Windows
// group disambiguates them from the Snapping.Zones.Border keys of the same spelling).
// FocusFadeDuration is a clamped int: the decoration focus cross-fade in ms
// (uSurfaceFocused ramp), 0 = instant.

void appendWindowsSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    // The "Apply to" scope is a closed set of tokens the Appearance page and the
    // effect agree on ("tiled" / "normal" / "all"); snap an unknown on-disk token
    // to the key's own default so garbage can't reach the effect.
    namespace WAS = ::PhosphorCompositor::WindowAppearanceScope;
    const auto scopeValidator = [](const QString& fallback) {
        return validStringOr({WAS::Tiled, WAS::Normal, WAS::All}, fallback);
    };
    schema.groups[CD::windowsAppearanceGroup()] = {
        {CD::showBorderKey(), CD::showWindowBorder(), QMetaType::Bool},
        {CD::borderScopeKey(),
         CD::windowBorderScope(),
         QMetaType::QString,
         {},
         scopeValidator(CD::windowBorderScope()),
         tokenChoices({WAS::Tiled, WAS::Normal, WAS::All})},
        {CD::widthKey(),
         CD::windowBorderWidth(),
         QMetaType::Int,
         {},
         clampInt(CD::windowBorderWidthMin(), CD::windowBorderWidthMax())},
        {CD::radiusKey(),
         CD::windowBorderRadius(),
         QMetaType::Int,
         {},
         clampInt(CD::windowBorderRadiusMin(), CD::windowBorderRadiusMax())},
        {CD::borderColorActiveKey(),
         CD::windowBorderColorActive(),
         QMetaType::QString,
         {},
         validBorderColorOr(CD::windowBorderColorActive())},
        {CD::borderColorInactiveKey(),
         CD::windowBorderColorInactive(),
         QMetaType::QString,
         {},
         validBorderColorOr(CD::windowBorderColorInactive())},
        {CD::hideTitleBarsKey(), CD::hideWindowTitleBars(), QMetaType::Bool},
        {CD::titleBarScopeKey(),
         CD::windowTitleBarScope(),
         QMetaType::QString,
         {},
         scopeValidator(CD::windowTitleBarScope()),
         tokenChoices({WAS::Tiled, WAS::Normal, WAS::All})},
        {CD::focusFadeDurationKey(),
         CD::focusFadeDuration(),
         QMetaType::Int,
         {},
         clampInt(CD::focusFadeDurationMin(), CD::focusFadeDurationMax())},
        // Plain opacity+tint layer: opacity/strength are [0.0, 1.0] doubles,
        // the tint colour shares the border-colour shape (#AARRGGBB or the
        // accent sentinel) and the scope shares the closed token set.
        {CD::showOpacityTintKey(), CD::showWindowOpacityTint(), QMetaType::Bool},
        {CD::opacityTintScopeKey(),
         CD::windowOpacityTintScope(),
         QMetaType::QString,
         {},
         scopeValidator(CD::windowOpacityTintScope()),
         tokenChoices({WAS::Tiled, WAS::Normal, WAS::All})},
        {CD::opacityKey(),
         CD::windowOpacity(),
         QMetaType::Double,
         {},
         clampDouble(CD::windowOpacityMin(), CD::windowOpacityMax())},
        {CD::tintStrengthKey(),
         CD::windowTintStrength(),
         QMetaType::Double,
         {},
         clampDouble(CD::windowTintStrengthMin(), CD::windowTintStrengthMax())},
        {CD::tintColorKey(), CD::windowTintColor(), QMetaType::QString, {}, validBorderColorOr(CD::windowTintColor())},
    };
}

// ─── Gaps (shared inner/outer gap model) ────────────────────────────────────
// The single inter-window gap model used by BOTH snapping and tiling. Uniform
// inner/outer plus the per-side outer overrides (gated by UsePerSide). All ints
// clamped to the shared gap range.

void appendGapsSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::gapsGroup()] = {
        {CD::innerGapKey(), CD::innerGap(), QMetaType::Int, {}, clampInt(CD::innerGapMin(), CD::innerGapMax())},
        {CD::outerGapKey(), CD::outerGap(), QMetaType::Int, {}, clampInt(CD::outerGapMin(), CD::outerGapMax())},
        {CD::usePerSideOuterGapKey(), CD::usePerSideOuterGap(), QMetaType::Bool},
        {CD::outerGapTopKey(),
         CD::outerGapTop(),
         QMetaType::Int,
         {},
         clampInt(CD::outerGapTopMin(), CD::outerGapTopMax())},
        {CD::outerGapBottomKey(),
         CD::outerGapBottom(),
         QMetaType::Int,
         {},
         clampInt(CD::outerGapBottomMin(), CD::outerGapBottomMax())},
        {CD::outerGapLeftKey(),
         CD::outerGapLeft(),
         QMetaType::Int,
         {},
         clampInt(CD::outerGapLeftMin(), CD::outerGapLeftMax())},
        {CD::outerGapRightKey(),
         CD::outerGapRight(),
         QMetaType::Int,
         {},
         clampInt(CD::outerGapRightMin(), CD::outerGapRightMax())},
    };
}

// ─── Decorations ──────────────────────────────────────────────────────────────
// Per-surface decoration tree: a DecorationProfileTree (the user-applied surface
// shader-pack chain) keyed on a dot-path surface namespace, persisted as a nested
// JSON object — same QVariantMap storage shape as the autotile PerAlgorithmSettings
// entry above and the animation ShaderProfileTree blob, with no sanitizer because
// the per-pack override schema is not known to the config layer. The blob is a
// leaf key under Decorations, mirroring ShaderProfileTree under Animations; the
// Decorations.WindowFiltering sub-group is registered separately.

void appendDecorationsSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::decorationsGroup()] = {
        // Per-surface decoration tree. The stored default is the EMPTY tree —
        // the blob holds only user edits. The built-in card chrome for the OSD
        // and PopupFrame popups (ConfigDefaults::decorationProfileTree) is NOT
        // persisted: Settings overlays it as a lowest-precedence seed layer on
        // every read (withSeedDefaults), so shipped default updates keep
        // flowing to configs that never customized those surfaces.
        {CD::decorationProfileTreeKey(), PhosphorSurfaceShaders::DecorationProfileTree().toJson().toVariantMap(),
         QMetaType::QVariantMap},
    };
    // What the decoration chain is allowed to keep redrawing. An animated pack
    // repaints every window carrying it on every vsync, which never lets the GPU
    // leave its top performance state.
    schema.groups[CD::decorationsPerformanceGroup()] = {
        {CD::animateFocusedOnlyKey(), CD::decorationAnimateFocusedOnly(), QMetaType::Bool},
        {CD::pauseWhenIdleKey(), CD::decorationPauseWhenIdle(), QMetaType::Bool},
        // Clamped here, not in the UI. P_STORE_SET_INT delegates range enforcement
        // to the schema validator, and the daemon feeds this straight into an
        // ext-idle-notify-v1 timeout as `value * 1000` — a hand-edited 0 or -1 in
        // config.json would otherwise arm a nonsensical timer (fire-immediately, or
        // rejected outright, so the pause never engages). The slider's from/to are a
        // UI affordance, not a validation boundary.
        {CD::idleTimeoutSecKey(),
         CD::decorationIdleTimeoutSec(),
         QMetaType::Int,
         {},
         clampInt(CD::decorationIdleTimeoutSecMin(), CD::decorationIdleTimeoutSecMax())},
    };
}

const PhosphorConfig::Schema& cachedSettingsSchema()
{
    // One immortal copy for read-only consumers (picker options, value
    // labels). Settings itself keeps building a fresh Schema, since it hands
    // ownership to its Store.
    static const PhosphorConfig::Schema schema = buildSettingsSchema();
    return schema;
}

} // namespace PlasmaZones
