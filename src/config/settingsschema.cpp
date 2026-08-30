// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE SIZE: this TU sits in the 1000-1150 grace band and stays whole
// deliberately: it is a flat sequence of one appendXxxSchema function per
// config domain plus the validator helpers several of them share — one
// file-local (validStringOr), the rest at namespace scope in settingsschema_p.h
// or declared in settingsschema.h because the per-domain TUs share them too
// (canonicalCommaList, canonicalThemeFallbackColor, canonicalTriggerList).
// The domains big enough to carry their own weight are already split
// (settingsschema_scrolling.cpp's three entry points and
// settingsschema_tiling.cpp's one); every remaining function is under ninety
// lines, and moving one out drags its helpers into a header for a single
// consumer. When a domain grows past that, split it the way scrolling and
// tiling were — do not let this file cross the 1150 ceiling instead.

#include "settingsschema.h"

#include <QColor>

#include "settingsschemachoices.h"
#include "settingsschema_p.h"

#include "configdefaults.h"
#include "configmigration.h"
#include <PhosphorRules/ActionParams.h>
#include "core/types/enums.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <QtGlobal>
#include <PhosphorScreens/ScreenIdentity.h>

#include <iterator>

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
    appendEditorSchema(s);
    appendExclusionsSchema(s);
    appendDisplaySchema(s);
    appendZoneSelectorSchema(s);
    appendActivationSchema(s);
    appendBehaviorSchema(s);
    appendAutotilingSchema(s);
    appendScrollingSchema(s);
    appendScrollingZoneSelectorSchema(s);
    appendWindowsSchema(s);
    appendGapsSchema(s);
    appendDecorationsSchema(s);

    return s;
}

// ─── Validator helpers ──────────────────────────────────────────────────────
// Common coercion patterns factored to keep group schemas readable. Return
// the same function-object type as KeyDef::validator. The ones the scrolling
// and tiling TUs share live in settingsschema_p.h; the rest are local to this
// file.

using SchemaValidators::canonicalCommaList;
using SchemaValidators::canonicalFontFamily;
using SchemaValidators::clampDouble;
using SchemaValidators::clampInt;
using SchemaValidators::validIntOr;

namespace {
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

/// Both this cap and Settings::MaxTriggersPerAction resolve to
/// ConfigDefaults::maxTriggersPerAction() — single source of truth, no
/// drift possible because neither TU carries its own literal.
constexpr int kSchemaMaxTriggersPerAction = ConfigDefaults::maxTriggersPerAction();

} // namespace

/// Canonicalize a theme-fallback colour: the EMPTY sentinel ("follow the
/// theme") and any valid colour name pass through unchanged; anything else
/// maps back to empty rather than reaching QML as an invalid QColor. See the
/// header. The single validator behind EVERY theme-fallback colour key in
/// both schema TUs — the seven in this file (four zone, three Windows) and,
/// via namespace scope, the five in settingsschema_scrolling.cpp.
QVariant canonicalThemeFallbackColor(const QVariant& v)
{
    const QString s = v.toString();
    if (s.isEmpty() || QColor::isValidColorName(s)) {
        return s;
    }
    return QString();
}

/// Canonicalize a trigger list: cap size, coerce each entry to a
/// {modifier:int, mouseButton:int} QVariantMap. Runs on every read and
/// every write so the flush loop enforces the cap even when the setter
/// path is bypassed (e.g. a hand-edited config file carrying 12 entries).
/// Namespace scope (declared in settingsschema.h): shared with
/// settingsschema_scrolling.cpp and settingsschema_tiling.cpp, whose
/// Scrolling.Behavior and Tiling.Behavior groups carry the drag-insert
/// trigger lists. The Scrolling.Wheel.Focus and Scrolling.Wheel.View groups
/// go through canonicalWheelTriggerList instead, which wraps this one.
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
        // Field-level ok-flags, for the same drop-malformed reason as the
        // entry gate above: a partially-garbage entry ({modifier: "alt",
        // mouseButton: 1}) used to coerce its bad field to 0, and modifier 0
        // means "don't care" to the drag readers — silently widening a
        // configured "Alt + Left" trigger to "Left button, any modifier".
        bool modOk = false;
        bool btnOk = false;
        const int modifier = src.value(ConfigKeys::triggerModifierField(), 0).toInt(&modOk);
        const int mouseButton = src.value(ConfigKeys::triggerMouseButtonField(), 0).toInt(&btnOk);
        // Range half of the hardening, one step past the type (ok-flag)
        // half, in each field's OWN vocabulary: `modifier` is a DragModifier
        // ENUMERATOR (the drag readers switch on it), not a Qt modifier
        // mask, and `mouseButton` is a Qt::MouseButtons bitmask (matched
        // with `&`). A numeric-but-impossible field is dropped like a
        // malformed type instead of being persisted verbatim as a trigger
        // no event can ever match.
        // Qt::AllButtons, NOT Qt::MouseButtonMask: the mask is 0xffffffff,
        // whose int cast is -1, and `x & ~(-1)` is always 0 — a check that
        // rejects nothing. AllButtons is the real any-valid-button set, and
        // a negative int's sign bit lies outside it, so negatives fail too.
        if (!modOk || !btnOk || modifier < static_cast<int>(DragModifier::Disabled) || modifier > MaxDragModifier
            || (mouseButton & ~static_cast<int>(Qt::AllButtons)) != 0) {
            continue;
        }
        QVariantMap canon;
        canon[ConfigKeys::triggerModifierField()] = modifier;
        canon[ConfigKeys::triggerMouseButtonField()] = mouseButton;
        out.append(canon);
    }
    return QVariant(out);
}

/// Canonicalize a WHEEL chord list: canonicalTriggerList, then drop any entry
/// naming DragModifier::AlwaysActive, zero the mouse button on the entries
/// that remain, and drop any entry left with no modifier at all.
///
/// AlwaysActive is a drag-only sentinel and it does not survive the move to
/// exact matching. modifierMaskFor has no case for it, so it folds to
/// Qt::NoModifier, and exactModifierMatch then reads the entry as "match
/// only when NO chord modifier is held" — the exact inverse of the "match
/// whatever is held" the drag readers give it. Left in a wheel list it does
/// not mean "always": it silently claims every unmodified wheel event over a
/// strip screen and turns it into a column step, with the app underneath
/// getting nothing.
///
/// Dropped here rather than in canonicalTriggerList, which the drag lists
/// share and which must keep storing the sentinel (TriggerUtils builds it
/// deliberately for the always-active drag case).
QVariant canonicalWheelTriggerList(const QVariant& v)
{
    const QVariantList canon = canonicalTriggerList(v).toList();
    QVariantList out;
    out.reserve(canon.size());
    for (const QVariant& entry : canon) {
        const QVariantMap src = entry.toMap();
        const int modifier = src.value(ConfigKeys::triggerModifierField(), 0).toInt();
        if (modifier == static_cast<int>(DragModifier::AlwaysActive)) {
            continue;
        }
        // A wheel chord is modifiers only, so the mouse button is dropped
        // rather than stored. The exact matcher compares buttons as a SUBSET
        // even though it compares modifiers exactly, which means a
        // modifier-only chord shadows the same modifier plus a button and the
        // longer binding could never be reached. The settings rows offer
        // modifiers only for that reason; enforcing it here too is what makes
        // it an invariant rather than a UI convention, and it also cleans up a
        // button left behind by a config written before the rows were
        // narrowed. An entry that was ONLY a button has nothing left and goes.
        if (modifier == static_cast<int>(DragModifier::Disabled)) {
            continue;
        }
        QVariantMap canonEntry;
        canonEntry[ConfigKeys::triggerModifierField()] = modifier;
        canonEntry[ConfigKeys::triggerMouseButtonField()] = 0;
        out.append(canonEntry);
    }
    return QVariant(out);
}

// ─── Shaders ────────────────────────────────────────────────────────────────
// Controls: frame rate, plus the full audio-spectrum parameter set in the
// Shaders.Audio sub-group. String choices are coerced by the ConfigDefaults
// normalizers so hand-edited configs can't persist garbage; the input source
// is free-form (the provider sanitizes it before it reaches cava).

void appendShadersSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::shadersGroup()] = {
        {CD::frameRateKey(), CD::shaderFrameRate(), QMetaType::Int,
         QStringLiteral("Target refresh rate for shader animations."),
         clampInt(CD::shaderFrameRateMin(), CD::shaderFrameRateMax())},
    };
    schema.groups[CD::shadersAudioGroup()] = {
        {CD::enabledKey(), CD::enableAudioVisualizer(), QMetaType::Bool,
         QStringLiteral("Capture system audio so the audio-reactive shader packs have a signal to follow. Off, those "
                        "shaders render but stay still.")},
        {CD::barsKey(), CD::audioSpectrumBarCount(), QMetaType::Int,
         QStringLiteral("Number of frequency bands in the audio visualization."),
         clampInt(CD::audioSpectrumBarCountMin(), CD::audioSpectrumBarCountMax())},
        {CD::autosensKey(), CD::audioAutosens(), QMetaType::Bool,
         QStringLiteral("Continuously adjusts sensitivity so the bars fill the available range.")},
        {CD::sensitivityKey(), CD::audioSensitivity(), QMetaType::Int,
         QStringLiteral("Gain applied to the audio signal. With automatic gain on, this is the level it starts "
                        "adapting from."),
         clampInt(CD::audioSensitivityMin(), CD::audioSensitivityMax())},
        {CD::noiseReductionKey(), CD::audioNoiseReduction(), QMetaType::Int,
         QStringLiteral("How smoothly the bars respond. Higher values are slower and calmer while lower values are "
                        "fast and twitchy."),
         clampInt(CD::audioNoiseReductionMin(), CD::audioNoiseReductionMax())},
        {CD::lowerCutoffHzKey(), CD::audioLowerCutoffHz(), QMetaType::Int,
         QStringLiteral("Sounds below this frequency are ignored."),
         clampInt(CD::audioLowerCutoffHzMin(), CD::audioLowerCutoffHzMax())},
        {CD::higherCutoffHzKey(), CD::audioHigherCutoffHz(), QMetaType::Int,
         QStringLiteral("Sounds above this frequency are ignored."),
         clampInt(CD::audioHigherCutoffHzMin(), CD::audioHigherCutoffHzMax())},
        {CD::monstercatKey(), CD::audioMonstercat(), QMetaType::Bool,
         QStringLiteral("Spreads each bar into its neighbours for a smoother outline.")},
        {CD::wavesKey(), CD::audioWaves(), QMetaType::Bool, QStringLiteral("Rounds the spectrum into soft waves.")},
        {CD::channelModeKey(), CD::audioChannelMode(), QMetaType::QString,
         QStringLiteral("Stereo shows left and right bars side by side. Mono collapses to one set of bars."),
         [](const QVariant& v) {
             return QVariant(CD::normalizeAudioChannelMode(v.toString()));
         },
         tokenChoices(CD::audioChannelModeOptions())},
        {CD::reverseKey(), CD::audioReverse(), QMetaType::Bool,
         QStringLiteral("Flip the frequency order of the bars.")},
        {CD::extraSmoothingKey(), CD::audioExtraSmoothing(), QMetaType::Int,
         QStringLiteral("Additional smoothing applied on top of noise reduction."),
         clampInt(CD::audioExtraSmoothingMin(), CD::audioExtraSmoothingMax())},
        {CD::inputMethodKey(), CD::audioInputMethod(), QMetaType::QString,
         QStringLiteral("Leave on Automatic unless capture fails with the detected backend."),
         [](const QVariant& v) {
             return QVariant(CD::normalizeAudioInputMethod(v.toString()));
         },
         tokenChoices(CD::audioInputMethodOptions())},
        {CD::inputSourceKey(), CD::audioInputSource(), QMetaType::QString,
         QStringLiteral("Capture device or monitor source. Keep it set to auto to follow the default output.")},
    };
}

// ─── Appearance ─────────────────────────────────────────────────────────────
// Declares four zone-overlay sub-groups under Snapping.Zones.*: Colors (3
// theme-fallback zone colors), Labels (font family/color/scale/weight +
// italic/underline/strikeout toggles), Opacity (active + inactive), Border
// (width + radius). The per-mode snapped-window
// decoration groups that used to live here are gone — window border and title-bar
// appearance moved to the top-level mode-neutral Windows config group (see
// appendWindowsSchema).

void appendAppearanceSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    // Theme-fallback colour keys — the three zone colours here AND the
    // Labels FontColor below: stored as strings where EMPTY means "follow
    // the system palette" (the same sentinel the scrolling colour keys
    // use); Settings resolves in the getters.
    schema.groups[CD::snappingZonesColorsGroup()] = {
        {CD::highlightKey(), CD::themeFallbackColorDefault(), QMetaType::QString,
         QStringLiteral("Colour of the zone under the cursor. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        {CD::inactiveKey(), CD::themeFallbackColorDefault(), QMetaType::QString,
         QStringLiteral("Colour of the zones not under the cursor. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        {CD::borderKey(), CD::themeFallbackColorDefault(), QMetaType::QString,
         QStringLiteral("Colour of the zone borders. Empty follows the colour scheme."), canonicalThemeFallbackColor},
    };

    schema.groups[CD::snappingZonesLabelsGroup()] = {
        {CD::fontColorKey(), CD::themeFallbackColorDefault(), QMetaType::QString,
         QStringLiteral("Colour of the zone label text. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        {CD::fontFamilyKey(), CD::labelFontFamily(), QMetaType::QString,
         QStringLiteral("Typeface for zone labels. Empty follows the system font."),
         canonicalFontFamily(PhosphorRules::MaxFontFamilyLength)},
        {CD::fontSizeScaleKey(), CD::labelFontSizeScale(), QMetaType::Double,
         QStringLiteral("Size multiplier for zone label text."),
         clampDouble(CD::labelFontSizeScaleMin(), CD::labelFontSizeScaleMax())},
        {CD::fontWeightKey(), CD::labelFontWeight(), QMetaType::Int,
         QStringLiteral("Weight of the zone label text, on the usual 100 to 900 scale where 400 is regular and 700 is "
                        "bold."),
         clampInt(CD::labelFontWeightMin(), CD::labelFontWeightMax())},
        {CD::fontItalicKey(), CD::labelFontItalic(), QMetaType::Bool, QStringLiteral("Italicize the zone label text.")},
        {CD::fontUnderlineKey(), CD::labelFontUnderline(), QMetaType::Bool,
         QStringLiteral("Underline the zone label text.")},
        {CD::fontStrikeoutKey(), CD::labelFontStrikeout(), QMetaType::Bool,
         QStringLiteral("Strike through the zone label text.")},
    };

    schema.groups[CD::snappingZonesOpacityGroup()] = {
        {CD::activeKey(), CD::activeOpacity(), QMetaType::Double,
         QStringLiteral("Opacity of the zone under the cursor."),
         clampDouble(CD::activeOpacityMin(), CD::activeOpacityMax())},
        {CD::inactiveKey(), CD::inactiveOpacity(), QMetaType::Double,
         QStringLiteral("Opacity of zones not under the cursor."),
         clampDouble(CD::inactiveOpacityMin(), CD::inactiveOpacityMax())},
    };

    schema.groups[CD::snappingZonesBorderGroup()] = {
        {CD::widthKey(), CD::borderWidth(), QMetaType::Int, QStringLiteral("Thickness of zone borders in pixels."),
         clampInt(CD::borderWidthMin(), CD::borderWidthMax())},
        {CD::radiusKey(), CD::borderRadius(), QMetaType::Int, QStringLiteral("Corner rounding of zones in pixels."),
         clampInt(CD::borderRadiusMin(), CD::borderRadiusMax())},
    };
    // The Effects group (display-OSD keys) is declared in one shot by
    // appendDisplaySchema below to avoid split-across-two-call-sites
    // ordering bugs.
}

// ─── Ordering ───────────────────────────────────────────────────────────────
// User-defined sort order for the layout picker, the tiling algorithm menu
// and the scrolling template picker. All are comma-joined lists on disk; the
// canonicalCommaList validator normalizes formatting (trim, de-dupe) on every
// read/write.

void appendOrderingSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::orderingGroup()] = {
        {CD::snappingLayoutOrderKey(), CD::snappingLayoutOrder(), QMetaType::QString,
         QStringLiteral("The order zone layouts appear in, as a comma-separated list of layout ids. Anything not "
                        "listed follows in its default order."),
         canonicalCommaList},
        {CD::tilingAlgorithmOrderKey(), CD::tilingAlgorithmOrder(), QMetaType::QString,
         QStringLiteral("The order autotile algorithms appear in, as a comma-separated list of algorithm ids. Anything "
                        "not listed follows in its default order."),
         canonicalCommaList},
        {CD::scrollingTemplateOrderKey(), CD::scrollingTemplateOrder(), QMetaType::QString,
         QStringLiteral("The order scrolling templates appear in, as a comma-separated list of template ids. Anything "
                        "not listed follows in its default order."),
         canonicalCommaList},
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
// at the library level (Profile::effective* + WindowAnimator::clampProfile
// on the hot path) rather than in the schema, because the
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
        {CD::enabledKey(), CD::animationsEnabled(), QMetaType::Bool,
         QStringLiteral("Whether window transitions play at all. Off, windows appear and disappear with no motion.")},
        // Profile and ShaderProfileTree persist as nested JSON objects
        // (QVariantMap) so the on-disk config shows their structure
        // directly. Existing string-blob configs are migrated transparently
        // by Store::read's legacy-string fallback on first load.
        {CD::animationProfileKey(), CD::animationProfile(sSchemaRegistry), QMetaType::QVariantMap,
         QStringLiteral("The active motion profile, holding its easing curve, duration, stagger interval, and sequence "
                        "mode. The animations page writes this, so it is not meant to be edited by hand.")},
        {CD::shaderProfileTreeKey(), CD::shaderProfileTree(), QMetaType::QVariantMap,
         QStringLiteral("Per-context overrides of which animation shader each transition uses. The animations page "
                        "writes this, so it is not meant to be edited by hand.")},
    };
}

// ─── Rendering ──────────────────────────────────────────────────────────────
// Two keys: the graphics API backend token and the GPU device pin. Each has
// its own coercing validator (normalizeRenderingBackend / normalizeGpuDevice)
// so hand-edited configs can't persist garbage.

void appendRenderingSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::renderingGroup()] = {
        {CD::backendKey(), CD::renderingBackend(), QMetaType::QString,
         QStringLiteral("Graphics API used for overlay rendering."),
         [](const QVariant& v) {
             return QVariant(CD::normalizeRenderingBackend(v.toString()));
         },
         tokenChoices(CD::renderingBackendOptions())},
        // Gpu is a free string, not a token enum: the legal values are the
        // machine's GPUs ("auto" or a "vendor:device" hex PCI pair), so there
        // are no declared choices — the picker enumerates DRM render nodes at
        // runtime. The validator still coerces malformed strings to "auto".
        {CD::gpuKey(), CD::gpuDevice(), QMetaType::QString,
         QStringLiteral("GPU that draws the zone overlays and on-screen displays. Automatic lets the graphics driver "
                        "decide. KWin composites window contents, so those are unaffected."),
         [](const QVariant& v) {
             return QVariant(CD::normalizeGpuDevice(v.toString()));
         }},
    };
}

// ─── Performance ────────────────────────────────────────────────────────────
// Poll interval + minimum zone size thresholds. All clamped ints.

void appendPerformanceSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::performanceGroup()] = {
        {CD::pollIntervalMsKey(), CD::pollIntervalMs(), QMetaType::Int,
         QStringLiteral("How often the daemon samples window positions while dragging. Lower reacts sooner and costs "
                        "more CPU."),
         clampInt(CD::pollIntervalMsMin(), CD::pollIntervalMsMax())},
        {CD::minimumZoneSizePxKey(), CD::minimumZoneSizePx(), QMetaType::Int,
         QStringLiteral("Zones smaller than this on either side are treated as unusable and skipped when placing a "
                        "window."),
         clampInt(CD::minimumZoneSizePxMin(), CD::minimumZoneSizePxMax())},
        {CD::minimumZoneDisplaySizePxKey(), CD::minimumZoneDisplaySizePx(), QMetaType::Int,
         QStringLiteral("Zones smaller than this on either side are left out of the overlay, so slivers in a dense "
                        "layout do not clutter it."),
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
        {CD::adjacentThresholdKey(), CD::adjacentThreshold(), QMetaType::Int,
         QStringLiteral("Distance from zone edge for multi-zone selection."),
         clampInt(CD::adjacentThresholdMin(), CD::adjacentThresholdMax())},
    };
}

// ─── Shortcuts ──────────────────────────────────────────────────────────────
// TWO sub-groups: Global (editor/settings launchers, zone navigation,
// snap-to-zone numbered slots, layout rotation/swap, virtual-screen rotation)
// and Tiling (autotile master/ratio/count controls + retile toggle). All
// QString keys, no validators needed. There is no Editor group here — the
// zone editor's shortcuts are not part of the daemon's schema; EditorController
// owns its own settings in a separate process.

namespace {
// Helper: append a string KeyDef with no validator. Cuts the noise in the
// schema below when every entry is the same shape.
//
// `description` is what the action DOES, in the user's terms, for the
// settings UI and the generated documentation to share. Every call below
// passes one. The parameter stays optional so an entry added before anyone
// has written its sentence still compiles; the schema dump emits the field
// unconditionally, so a blank one is a query away rather than a source grep.
inline void addShortcut(QVector<PhosphorConfig::KeyDef>& list, const QString& key, const QString& defaultValue,
                        const QString& description = {})
{
    list.append({key, defaultValue, QMetaType::QString, description});
}
} // namespace

void appendShortcutsSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;

    QVector<PhosphorConfig::KeyDef> globals;
    addShortcut(globals, CD::openEditorKey(), CD::openEditorShortcut(), QStringLiteral("Open zone editor."));
    addShortcut(globals, CD::openSettingsKey(), CD::openSettingsShortcut(), QStringLiteral("Open settings."));
    addShortcut(globals, CD::toggleCheatsheetKey(), CD::toggleCheatsheetShortcut(),
                QStringLiteral("Open the shortcut cheatsheet."));
    addShortcut(globals, CD::previousLayoutKey(), CD::previousLayoutShortcut(),
                QStringLiteral("Switches this screen to the previous layout in the list."));
    addShortcut(globals, CD::nextLayoutKey(), CD::nextLayoutShortcut(),
                QStringLiteral("Switches this screen to the next layout in the list."));
    const QString quickDefaults[] = {
        CD::quickLayout1Shortcut(), CD::quickLayout2Shortcut(), CD::quickLayout3Shortcut(),
        CD::quickLayout4Shortcut(), CD::quickLayout5Shortcut(), CD::quickLayout6Shortcut(),
        CD::quickLayout7Shortcut(), CD::quickLayout8Shortcut(), CD::quickLayout9Shortcut(),
    };
    // Bound by the protocol constant, not a local 9: quickLayoutKey() qFatals
    // outside [1, QuickLayoutSlotCount], so a raised constant with a stale
    // local literal would silently declare too few keys and a LOWERED one
    // would abort at startup. The static_assert makes the defaults array
    // track the constant at compile time instead.
    static_assert(std::size(quickDefaults) == PhosphorProtocol::Service::QuickLayoutSlotCount,
                  "quick-layout defaults array must cover every protocol slot");
    for (int i = 0; i < PhosphorProtocol::Service::QuickLayoutSlotCount; ++i) {
        addShortcut(globals, CD::quickLayoutKey(i + 1), quickDefaults[i],
                    QStringLiteral("Loads quick-layout slot %1 on the focused screen. A slot holds a zone layout, an "
                                   "autotile algorithm, or a scrolling template, depending on the screen's mode.")
                        .arg(i + 1));
    }
    addShortcut(globals, CD::moveWindowLeftKey(), CD::moveWindowLeftShortcut(),
                QStringLiteral("Moves the focused window into the zone to the left of its current one."));
    addShortcut(globals, CD::moveWindowRightKey(), CD::moveWindowRightShortcut(),
                QStringLiteral("Moves the focused window into the zone to the right of its current one."));
    addShortcut(globals, CD::moveWindowUpKey(), CD::moveWindowUpShortcut(),
                QStringLiteral("Moves the focused window into the zone above its current one."));
    addShortcut(globals, CD::moveWindowDownKey(), CD::moveWindowDownShortcut(),
                QStringLiteral("Moves the focused window into the zone below its current one."));
    addShortcut(globals, CD::focusZoneLeftKey(), CD::focusZoneLeftShortcut(),
                QStringLiteral("Moves focus to the window in the zone to the left of the focused one."));
    addShortcut(globals, CD::focusZoneRightKey(), CD::focusZoneRightShortcut(),
                QStringLiteral("Moves focus to the window in the zone to the right of the focused one."));
    addShortcut(globals, CD::focusZoneUpKey(), CD::focusZoneUpShortcut(),
                QStringLiteral("Moves focus to the window in the zone above the focused one."));
    addShortcut(globals, CD::focusZoneDownKey(), CD::focusZoneDownShortcut(),
                QStringLiteral("Moves focus to the window in the zone below the focused one."));
    addShortcut(globals, CD::pushToEmptyZoneKey(), CD::pushToEmptyZoneShortcut(),
                QStringLiteral("Push focused window to the nearest empty zone."));
    addShortcut(globals, CD::restoreWindowSizeKey(), CD::restoreWindowSizeShortcut(),
                QStringLiteral("Restore focused window to its pre-snap size."));
    addShortcut(globals, CD::toggleWindowFloatKey(), CD::toggleWindowFloatShortcut(),
                QStringLiteral("Toggle focused window's floating state."));
    addShortcut(
        globals, CD::switchFocusFloatTilingKey(), CD::switchFocusFloatTilingShortcut(),
        QStringLiteral("Moves focus between the floating windows and the placed layout. It returns to the window that "
                       "last had focus there when that window is still available."));
    addShortcut(globals, CD::swapWindowLeftKey(), CD::swapWindowLeftShortcut(),
                QStringLiteral("Swaps the focused window with the window in the zone to the left of it."));
    addShortcut(globals, CD::swapWindowRightKey(), CD::swapWindowRightShortcut(),
                QStringLiteral("Swaps the focused window with the window in the zone to the right of it."));
    addShortcut(globals, CD::swapWindowUpKey(), CD::swapWindowUpShortcut(),
                QStringLiteral("Swaps the focused window with the window in the zone above it."));
    addShortcut(globals, CD::swapWindowDownKey(), CD::swapWindowDownShortcut(),
                QStringLiteral("Swaps the focused window with the window in the zone below it."));
    addShortcut(
        globals, CD::spanWindowLeftKey(), CD::spanWindowLeftShortcut(),
        QStringLiteral("Extends the focused window across the adjacent zone to the left of it. Once the span reaches "
                       "the edge of the layout, the same key pulls the opposite edge in instead."));
    addShortcut(
        globals, CD::spanWindowRightKey(), CD::spanWindowRightShortcut(),
        QStringLiteral("Extends the focused window across the adjacent zone to the right of it. Once the span reaches "
                       "the edge of the layout, the same key pulls the opposite edge in instead."));
    addShortcut(
        globals, CD::spanWindowUpKey(), CD::spanWindowUpShortcut(),
        QStringLiteral("Extends the focused window across the adjacent zone above it. Once the span reaches the edge "
                       "of the layout, the same key pulls the opposite edge in instead."));
    addShortcut(
        globals, CD::spanWindowDownKey(), CD::spanWindowDownShortcut(),
        QStringLiteral("Extends the focused window across the adjacent zone below it. Once the span reaches the edge "
                       "of the layout, the same key pulls the opposite edge in instead."));
    const QString snapToZoneDefaults[] = {
        CD::snapToZone1Shortcut(), CD::snapToZone2Shortcut(), CD::snapToZone3Shortcut(),
        CD::snapToZone4Shortcut(), CD::snapToZone5Shortcut(), CD::snapToZone6Shortcut(),
        CD::snapToZone7Shortcut(), CD::snapToZone8Shortcut(), CD::snapToZone9Shortcut(),
    };
    // Same protocol-constant bound as the quick-layout loop above.
    static_assert(std::size(snapToZoneDefaults) == PhosphorProtocol::Service::QuickLayoutSlotCount,
                  "snap-to-zone defaults array must cover every protocol slot");
    for (int i = 0; i < PhosphorProtocol::Service::QuickLayoutSlotCount; ++i) {
        addShortcut(globals, CD::snapToZoneKey(i + 1), snapToZoneDefaults[i],
                    QStringLiteral("Snaps the focused window to zone %1 of the current layout.").arg(i + 1));
    }
    addShortcut(globals, CD::rotateWindowsClockwiseKey(), CD::rotateWindowsClockwiseShortcut(),
                QStringLiteral("Moves every window one zone clockwise within the current layout."));
    addShortcut(globals, CD::rotateWindowsCounterclockwiseKey(), CD::rotateWindowsCounterclockwiseShortcut(),
                QStringLiteral("Moves every window one zone counter-clockwise within the current layout."));
    addShortcut(globals, CD::cycleWindowForwardKey(), CD::cycleWindowForwardShortcut(),
                QStringLiteral("Moves the focused window to the next zone in the layout's order."));
    addShortcut(globals, CD::cycleWindowBackwardKey(), CD::cycleWindowBackwardShortcut(),
                QStringLiteral("Moves the focused window to the previous zone in the layout's order."));
    addShortcut(globals, CD::resnapToNewLayoutKey(), CD::resnapToNewLayoutShortcut(),
                QStringLiteral("Reapply current layout."));
    addShortcut(globals, CD::snapAllWindowsKey(), CD::snapAllWindowsShortcut(),
                QStringLiteral("Snap every visible window to its best-fit zone."));
    addShortcut(globals, CD::layoutPickerKey(), CD::layoutPickerShortcut(),
                QStringLiteral("Opens a picker to choose this screen's layout."));
    addShortcut(globals, CD::toggleLayoutLockKey(), CD::toggleLayoutLockShortcut(),
                QStringLiteral("Locks this screen's layout so nothing switches it until unlocked."));
    addShortcut(
        globals, CD::swapVirtualScreenLeftKey(), CD::swapVirtualScreenLeftShortcut(),
        QStringLiteral("Swaps this virtual screen's windows with those of the virtual screen to the left of it."));
    addShortcut(
        globals, CD::swapVirtualScreenRightKey(), CD::swapVirtualScreenRightShortcut(),
        QStringLiteral("Swaps this virtual screen's windows with those of the virtual screen to the right of it."));
    addShortcut(globals, CD::swapVirtualScreenUpKey(), CD::swapVirtualScreenUpShortcut(),
                QStringLiteral("Swaps this virtual screen's windows with those of the virtual screen above it."));
    addShortcut(globals, CD::swapVirtualScreenDownKey(), CD::swapVirtualScreenDownShortcut(),
                QStringLiteral("Swaps this virtual screen's windows with those of the virtual screen below it."));
    addShortcut(globals, CD::rotateVirtualScreensClockwiseKey(), CD::rotateVirtualScreensClockwiseShortcut(),
                QStringLiteral("Moves every virtual screen's windows one position clockwise."));
    addShortcut(globals, CD::rotateVirtualScreensCounterclockwiseKey(),
                CD::rotateVirtualScreensCounterclockwiseShortcut(),
                QStringLiteral("Moves every virtual screen's windows one position counter-clockwise."));
    schema.groups[CD::shortcutsGlobalGroup()] = std::move(globals);

    schema.groups[CD::shortcutsTilingGroup()] = {
        {CD::toggleKey(), CD::autotileToggleShortcut(), QMetaType::QString,
         QStringLiteral("Cycle the focused screen's placement mode.")},
        {CD::focusMasterKey(), CD::autotileFocusMasterShortcut(), QMetaType::QString,
         QStringLiteral("Focus the master window.")},
        {CD::swapMasterKey(), CD::autotileSwapMasterShortcut(), QMetaType::QString,
         QStringLiteral("Swap focused window with master.")},
        {CD::incMasterRatioKey(), CD::autotileIncMasterRatioShortcut(), QMetaType::QString,
         QStringLiteral("Gives the master area a larger share of the screen.")},
        {CD::decMasterRatioKey(), CD::autotileDecMasterRatioShortcut(), QMetaType::QString,
         QStringLiteral("Gives the master area a smaller share of the screen.")},
        {CD::incMasterCountKey(), CD::autotileIncMasterCountShortcut(), QMetaType::QString,
         QStringLiteral("Moves one more window into the master area.")},
        {CD::decMasterCountKey(), CD::autotileDecMasterCountShortcut(), QMetaType::QString,
         QStringLiteral("Moves one window out of the master area.")},
        {CD::retileKey(), CD::autotileRetileShortcut(), QMetaType::QString,
         QStringLiteral("Re-applies the tiling algorithm to every window on the screen.")},
    };

    // Shortcuts.Scrolling is declared by the scrolling TU (split out for
    // file-size) but still assembled from here, so the whole Shortcuts.*
    // family has one entry point.
    appendScrollingShortcutsSchema(schema);
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
        {CD::duplicateKey(), CD::editorDuplicateShortcut(), QMetaType::QString,
         QStringLiteral("Editor shortcut that clones the selected zone.")},
        {CD::splitHorizontalKey(), CD::editorSplitHorizontalShortcut(), QMetaType::QString,
         QStringLiteral("Editor shortcut that divides the selected zone into left and right halves.")},
        {CD::splitVerticalKey(), CD::editorSplitVerticalShortcut(), QMetaType::QString,
         QStringLiteral("Editor shortcut that divides the selected zone into top and bottom halves.")},
        {CD::fillKey(), CD::editorFillShortcut(), QMetaType::QString,
         QStringLiteral("Editor shortcut that expands the selected zone to fill the empty area around it.")},
    };

    auto modifierOr = [](int fallback) {
        return [fallback](const QVariant& v) -> QVariant {
            // Ok-flag so a non-numeric payload takes the fallback instead of
            // coercing to 0 == Qt::NoModifier, which the range check would
            // accept as a deliberate "no modifier".
            bool ok = false;
            const int raw = v.toInt(&ok);
            constexpr int valid = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
            if (ok && (raw == Qt::NoModifier || (raw & valid) == raw)) {
                return QVariant(raw);
            }
            return QVariant(fallback);
        };
    };

    schema.groups[CD::editorSnappingGroup()] = {
        {CD::gridEnabledKey(), CD::editorGridSnappingEnabled(), QMetaType::Bool,
         QStringLiteral("Snap zones to a grid while dragging or resizing them in the editor.")},
        {CD::edgeEnabledKey(), CD::editorEdgeSnappingEnabled(), QMetaType::Bool,
         QStringLiteral("Snap zones to the edges of neighbouring zones while dragging or resizing them.")},
        {CD::intervalXKey(), CD::editorSnapIntervalX(), QMetaType::Double,
         QStringLiteral("Horizontal grid spacing, as a fraction of the screen width."),
         clampDouble(CD::editorSnapIntervalMin(), CD::editorSnapIntervalMax())},
        {CD::intervalYKey(), CD::editorSnapIntervalY(), QMetaType::Double,
         QStringLiteral("Vertical grid spacing, as a fraction of the screen height."),
         clampDouble(CD::editorSnapIntervalMin(), CD::editorSnapIntervalMax())},
        {CD::overrideModifierKey(), CD::editorSnapOverrideModifier(), QMetaType::Int,
         QStringLiteral("Modifier held to bypass editor snapping for as long as it is down, as a Qt keyboard-modifier "
                        "value."),
         modifierOr(CD::editorSnapOverrideModifier())},
    };

    schema.groups[CD::editorFillOnDropGroup()] = {
        {CD::enabledKey(), CD::fillOnDropEnabled(), QMetaType::Bool,
         QStringLiteral("Let a zone dropped in the editor expand into the empty space around it.")},
        // Qt::KeyboardModifier BITMASK, not the DragModifier enum that the
        // identically named zone-span key uses — there is no closed choice
        // set to declare here.
        {CD::modifierKey(), CD::fillOnDropModifier(), QMetaType::Int,
         QStringLiteral("Modifier held while dropping a zone to expand it into available space, as a Qt "
                        "keyboard-modifier value."),
         modifierOr(CD::fillOnDropModifier())},
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
        {CD::transientWindowsKey(), CD::excludeTransientWindows(), QMetaType::Bool,
         QStringLiteral("Leave dialogs, popups, and toolbars where they are instead of placing them.")},
        {CD::minimumWindowWidthKey(), CD::minimumWindowWidth(), QMetaType::Int,
         QStringLiteral("Windows narrower than this are left alone by every placement mode. Zero turns the width "
                        "threshold off."),
         clampInt(CD::minimumWindowWidthMin(), CD::minimumWindowWidthMax())},
        {CD::minimumWindowHeightKey(), CD::minimumWindowHeight(), QMetaType::Int,
         QStringLiteral("Windows shorter than this are left alone by every placement mode. Zero turns the height "
                        "threshold off."),
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
        {CD::transientWindowsKey(), CD::animationExcludeTransientWindows(), QMetaType::Bool,
         QStringLiteral("Skip animations for dialogs, popups, tooltips, and dropdown menus.")},
        {CD::notificationsAndOsdKey(), CD::animationExcludeNotificationsAndOsd(), QMetaType::Bool,
         QStringLiteral("Skip animations for notification popups and on-screen displays such as volume and "
                        "brightness.")},
        {CD::minimumWindowWidthKey(), CD::animationMinimumWindowWidth(), QMetaType::Int,
         QStringLiteral("Windows narrower than this do not animate. Zero turns the width threshold off."),
         clampInt(CD::animationMinimumWindowWidthMin(), CD::animationMinimumWindowWidthMax())},
        {CD::minimumWindowHeightKey(), CD::animationMinimumWindowHeight(), QMetaType::Int,
         QStringLiteral("Windows shorter than this do not animate. Zero turns the height threshold off."),
         clampInt(CD::animationMinimumWindowHeightMin(), CD::animationMinimumWindowHeightMax())},
    };

    // Decoration window filtering — same shape as the Exclusions group,
    // stored independently so the KWin effect's border pass can be tuned
    // separately from snapping and animation filtering. Reuses the shared
    // leaf keys; only the group differs.
    schema.groups[CD::decorationsWindowFilteringGroup()] = {
        {CD::transientWindowsKey(), CD::decorationExcludeTransientWindows(), QMetaType::Bool,
         QStringLiteral("Skip decorations for dialogs, popups, and menus.")},
        {CD::minimumWindowWidthKey(), CD::decorationMinimumWindowWidth(), QMetaType::Int,
         QStringLiteral("Windows narrower than this get no decoration. Zero turns the width threshold off."),
         clampInt(CD::decorationMinimumWindowWidthMin(), CD::decorationMinimumWindowWidthMax())},
        {CD::minimumWindowHeightKey(), CD::decorationMinimumWindowHeight(), QMetaType::Int,
         QStringLiteral("Windows shorter than this get no decoration. Zero turns the height threshold off."),
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
        {CD::showOnAllMonitorsKey(), CD::showOnAllMonitors(), QMetaType::Bool,
         QStringLiteral("Display zone overlays on every monitor while dragging a window, not only the one under the "
                        "cursor.")},
        {CD::filterByAspectRatioKey(), CD::filterLayoutsByAspectRatio(), QMetaType::Bool,
         QStringLiteral("Only show layouts matching the current monitor's aspect ratio.")},
    };

    // Full Effects group declared here in one shot; declaring the whole
    // container from one call site keeps the schema build order-independent.
    // The retired blur toggle ("Blur") is intentionally NOT declared:
    // purgeStaleKeys() evicts the leftover key from existing user configs
    // on the next save().
    schema.groups[CD::snappingEffectsGroup()] = {
        {CD::showNumbersKey(), CD::showNumbers(), QMetaType::Bool,
         QStringLiteral("Display a number label inside each zone.")},
        {CD::flashOnSwitchKey(), CD::flashOnSwitch(), QMetaType::Bool,
         QStringLiteral("Briefly flash the zones when switching between layouts.")},
        {CD::osdOnLayoutSwitchKey(), CD::showOsdOnLayoutSwitch(), QMetaType::Bool,
         QStringLiteral("Show notification when switching between zone layouts.")},
        {CD::osdOnDesktopSwitchKey(), CD::showOsdOnDesktopSwitch(), QMetaType::Bool,
         QStringLiteral("Show notification on virtual desktop change, activity change, and daemon startup.")},
        {CD::navigationOsdKey(), CD::showNavigationOsd(), QMetaType::Bool,
         QStringLiteral("Show notification when moving windows with keyboard shortcuts.")},
        // validIntOr, not clampInt, per the enum-key convention documented in
        // settingsschema_p.h: qBound would reinterpret an out-of-range stored
        // value as the nearest enumerator instead of snapping to the default.
        {CD::osdStyleKey(), CD::osdStyle(), QMetaType::Int, QStringLiteral("Visual style of on-screen notifications."),
         validIntOr(
             {static_cast<int>(OsdStyle::None), static_cast<int>(OsdStyle::Text), static_cast<int>(OsdStyle::Preview)},
             CD::osdStyle()),
         intChoices({{static_cast<int>(OsdStyle::None), "none"_L1},
                     {static_cast<int>(OsdStyle::Text), "text"_L1},
                     {static_cast<int>(OsdStyle::Preview), "preview"_L1}})},
        {CD::overlayDisplayModeKey(), CD::overlayDisplayMode(), QMetaType::Int,
         QStringLiteral("How zones appear while dragging a window."),
         validIntOr({static_cast<int>(OverlayDisplayMode::ZoneRectangles),
                     static_cast<int>(OverlayDisplayMode::LayoutPreview)},
                    CD::overlayDisplayMode()),
         intChoices({{static_cast<int>(OverlayDisplayMode::ZoneRectangles), "zoneRectangles"_L1},
                     {static_cast<int>(OverlayDisplayMode::LayoutPreview), "layoutPreview"_L1}})},
    };

    // Zone-overlay shader assignments — one nested JSON blob (baseline +
    // per-layout overrides), persisted as a QVariantMap like the animation
    // ShaderProfileTree entry, with no sanitizer for the same reason.
    schema.groups[CD::snappingOverlayShadersGroup()] = {
        {CD::overlayShaderTreeKey(), CD::overlayShaderTree(), QMetaType::QVariantMap},
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
        {CD::enabledKey(), CD::zoneSelectorEnabled(), QMetaType::Bool,
         QStringLiteral("Show a layout picker when a window is dragged to a screen edge.")},
        {CD::triggerDistanceKey(), CD::triggerDistance(), QMetaType::Int,
         QStringLiteral("How close to the screen edge a drag has to come before the picker opens."),
         clampInt(CD::triggerDistanceMin(), CD::triggerDistanceMax())},
        // validIntOr for the three enum keys below, same convention note as
        // the Effects group's OSD enums.
        {CD::positionKey(), CD::position(), QMetaType::Int, QStringLiteral("Where on the screen the picker appears."),
         validIntOr({static_cast<int>(ZoneSelectorPosition::TopLeft), static_cast<int>(ZoneSelectorPosition::Top),
                     static_cast<int>(ZoneSelectorPosition::TopRight), static_cast<int>(ZoneSelectorPosition::Left),
                     static_cast<int>(ZoneSelectorPosition::Center), static_cast<int>(ZoneSelectorPosition::Right),
                     static_cast<int>(ZoneSelectorPosition::BottomLeft), static_cast<int>(ZoneSelectorPosition::Bottom),
                     static_cast<int>(ZoneSelectorPosition::BottomRight)},
                    CD::position()),
         intChoices({{static_cast<int>(ZoneSelectorPosition::TopLeft), "topLeft"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Top), "top"_L1},
                     {static_cast<int>(ZoneSelectorPosition::TopRight), "topRight"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Left), "left"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Center), "center"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Right), "right"_L1},
                     {static_cast<int>(ZoneSelectorPosition::BottomLeft), "bottomLeft"_L1},
                     {static_cast<int>(ZoneSelectorPosition::Bottom), "bottom"_L1},
                     {static_cast<int>(ZoneSelectorPosition::BottomRight), "bottomRight"_L1}})},
        {CD::layoutModeKey(), CD::layoutMode(), QMetaType::Int,
         QStringLiteral("How layout previews are arranged in the popup."),
         validIntOr({static_cast<int>(ZoneSelectorLayoutMode::Grid),
                     static_cast<int>(ZoneSelectorLayoutMode::Horizontal),
                     static_cast<int>(ZoneSelectorLayoutMode::Vertical)},
                    CD::layoutMode()),
         intChoices({{static_cast<int>(ZoneSelectorLayoutMode::Grid), "grid"_L1},
                     {static_cast<int>(ZoneSelectorLayoutMode::Horizontal), "horizontal"_L1},
                     {static_cast<int>(ZoneSelectorLayoutMode::Vertical), "vertical"_L1}})},
        {CD::previewWidthKey(), CD::previewWidth(), QMetaType::Int,
         QStringLiteral("Width of each layout preview in the picker. Only applies when the size mode is manual."),
         clampInt(CD::previewWidthMin(), CD::previewWidthMax())},
        {CD::previewHeightKey(), CD::previewHeight(), QMetaType::Int,
         QStringLiteral("Height of each layout preview in the picker. Only applies when the size mode is manual and "
                        "the aspect ratio is unlocked."),
         clampInt(CD::previewHeightMin(), CD::previewHeightMax())},
        {CD::previewLockAspectKey(), CD::previewLockAspect(), QMetaType::Bool,
         QStringLiteral("Derive the preview height from its width using the screen's aspect ratio, so previews match "
                        "the shape of the screen.")},
        {CD::gridColumnsKey(), CD::gridColumns(), QMetaType::Int, QStringLiteral("Number of layout previews per row."),
         clampInt(CD::gridColumnsMin(), CD::gridColumnsMax())},
        {CD::sizeModeKey(), CD::sizeMode(), QMetaType::Int,
         QStringLiteral("Whether preview size is chosen for you or taken from the width and height below."),
         validIntOr({static_cast<int>(ZoneSelectorSizeMode::Auto), static_cast<int>(ZoneSelectorSizeMode::Manual)},
                    CD::sizeMode()),
         intChoices({{static_cast<int>(ZoneSelectorSizeMode::Auto), "auto"_L1},
                     {static_cast<int>(ZoneSelectorSizeMode::Manual), "manual"_L1}})},
        {CD::maxRowsKey(), CD::maxRows(), QMetaType::Int,
         QStringLiteral("Most rows of previews the picker shows at once. It scrolls when there are more."),
         clampInt(CD::maxRowsMin(), CD::maxRowsMax())},
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
        {CD::enabledKey(), CD::snappingEnabled(), QMetaType::Bool,
         QStringLiteral("Whether snapping mode can be used at all. Off, it is skipped when cycling a screen's "
                        "placement mode.")},
    };
    // Snapping.Behavior owns FIVE scalar keys directly — Triggers,
    // ToggleActivation, ReleaseGraceMs, FocusNewWindows and FocusFollowsMouse — while the
    // SnapAssist / ZoneSpan / WindowHandling / Display / AutotileDragInsert
    // sub-groups each get their own Schema entry below (or already migrated).
    schema.groups[CD::snappingBehaviorGroup()] = {
        {CD::triggersKey(), CD::dragActivationTriggers(), QMetaType::QVariantList,
         QStringLiteral("Modifier and mouse-button combinations that show the zone overlay while a window is being "
                        "dragged. Each entry is a {modifier, mouseButton} pair."),
         canonicalTriggerList},
        {CD::toggleActivationKey(), CD::toggleActivation(), QMetaType::Bool,
         QStringLiteral("Tap the trigger to turn the overlay on, and tap again to turn it off, instead of holding it "
                        "down.")},
        {CD::releaseGraceMsKey(), CD::dragActivationGraceMs(), QMetaType::Int,
         QStringLiteral("How long the overlay stays up after the trigger is released, so a brief slip does not cancel "
                        "the snap."),
         clampInt(CD::triggerGraceMsMin(), CD::triggerGraceMsMax())},
        {CD::focusNewWindowsKey(), CD::snappingFocusNewWindows(), QMetaType::Bool,
         QStringLiteral("Focus a window when it is automatically placed into a zone on open.")},
        {CD::focusFollowsMouseKey(), CD::snappingFocusFollowsMouse(), QMetaType::Bool,
         QStringLiteral("Moving the mouse pointer over a snapped window gives it focus.")},
    };
    schema.groups[CD::snappingBehaviorZoneSpanGroup()] = {
        {CD::enabledKey(), CD::zoneSpanEnabled(), QMetaType::Bool,
         QStringLiteral("Allow a window to be snapped across two or more adjacent zones.")},
        {CD::modifierKey(), CD::zoneSpanModifier(), QMetaType::Int,
         QStringLiteral("Modifier held while dragging to paint across zones."),
         // DragModifier int values are contiguous but not ordered — each is a
         // discrete named modifier combo. clampInt on an unknown value would
         // reinterpret e.g. 99 as the highest valid enum (CtrlMeta),
         // silently giving the user a much stricter capture rule than they
         // asked for. Snap-to-default (Disabled = 0) instead so upgrade-
         // mismatches or hand-edited configs fall back to "off" rather than
         // a semantically-unrelated neighbour.
         validIntOr({static_cast<int>(DragModifier::Disabled), static_cast<int>(DragModifier::Shift),
                     static_cast<int>(DragModifier::Ctrl), static_cast<int>(DragModifier::Alt),
                     static_cast<int>(DragModifier::Meta), static_cast<int>(DragModifier::CtrlAlt),
                     static_cast<int>(DragModifier::CtrlShift), static_cast<int>(DragModifier::AltShift),
                     static_cast<int>(DragModifier::AlwaysActive), static_cast<int>(DragModifier::AltMeta),
                     static_cast<int>(DragModifier::CtrlAltMeta), static_cast<int>(DragModifier::MetaShift),
                     static_cast<int>(DragModifier::CtrlMeta)},
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
                     {static_cast<int>(DragModifier::CtrlAltMeta), "ctrlAltMeta"_L1},
                     {static_cast<int>(DragModifier::MetaShift), "metaShift"_L1},
                     {static_cast<int>(DragModifier::CtrlMeta), "ctrlMeta"_L1}})},
        {CD::triggersKey(), CD::zoneSpanTriggers(), QMetaType::QVariantList,
         QStringLiteral("Modifier and mouse-button combinations that span a drag across adjacent zones. Each entry is "
                        "a {modifier, mouseButton} pair."),
         canonicalTriggerList},
        {CD::toggleActivationKey(), CD::zoneSpanToggleMode(), QMetaType::Bool,
         QStringLiteral("Tap the span trigger to start spanning, and tap again to stop, instead of holding it down.")},
        {CD::releaseGraceMsKey(), CD::zoneSpanGraceMs(), QMetaType::Int,
         QStringLiteral("How long spanning stays active after the trigger is released."),
         clampInt(CD::triggerGraceMsMin(), CD::triggerGraceMsMax())},
    };
}

// ─── Behavior ───────────────────────────────────────────────────────────────
// WindowHandling + SnapAssist sub-groups. The Autotile drag-insert triggers
// live in Tiling.Behavior and get scheduled under the Autotiling migration.

void appendBehaviorSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::snappingBehaviorWindowHandlingGroup()] = {
        {CD::keepOnResolutionChangeKey(), CD::keepWindowsInZonesOnResolutionChange(), QMetaType::Bool,
         QStringLiteral("Move windows back to their zones after the screen resolution changes.")},
        {CD::moveNewToLastZoneKey(), CD::moveNewWindowsToLastZone(), QMetaType::Bool,
         QStringLiteral("Snap every newly opened window into whichever zone you most recently snapped a window into.")},
        {CD::restoreOnUnsnapKey(), CD::restoreOriginalSizeOnUnsnap(), QMetaType::Bool,
         QStringLiteral("Return a window to its original size when it is dragged out of a zone.")},
        {CD::stickyWindowHandlingKey(), CD::snappingStickyWindowHandling(), QMetaType::Int,
         QStringLiteral("How to treat windows that appear on every desktop."),
         validIntOr({static_cast<int>(StickyWindowHandling::TreatAsNormal),
                     static_cast<int>(StickyWindowHandling::RestoreOnly),
                     static_cast<int>(StickyWindowHandling::IgnoreAll)},
                    CD::snappingStickyWindowHandling()),
         intChoices({{static_cast<int>(StickyWindowHandling::TreatAsNormal), "treatAsNormal"_L1},
                     {static_cast<int>(StickyWindowHandling::RestoreOnly), "restoreOnly"_L1},
                     {static_cast<int>(StickyWindowHandling::IgnoreAll), "ignoreAll"_L1}})},
        {CD::restoreOnLoginKey(), CD::restoreWindowsToZonesOnLogin(), QMetaType::Bool,
         QStringLiteral("When an app reopens, during the session or after a logout, return it to the zone it was last "
                        "snapped in.")},
        {CD::restoreFloatedOnLoginKey(), CD::snappingRestoreFloatedWindowsOnLogin(), QMetaType::Bool,
         QStringLiteral("When an unsnapped window reopens after a logout, return it to the position and monitor it was "
                        "on. A rule can opt individual windows in or out.")},
        {CD::keepFloatingAboveKey(), CD::snappingKeepFloatingAbove(), QMetaType::Bool,
         QStringLiteral("Keep the windows you float stacked above the windows snapped into zones. A rule that sets a "
                        "window layer takes precedence for the windows it matches.")},
        {CD::unfloatFallbackToZoneKey(), CD::snapUnfloatFallbackToZone(), QMetaType::Bool,
         QStringLiteral("When you unfloat a window that was never snapped, snap it to a fallback zone instead of "
                        "leaving it floating. The fallback is the last used zone, then the first empty one, then the "
                        "first zone.")},
        {CD::autoAssignAllLayoutsKey(), CD::autoAssignAllLayouts(), QMetaType::Bool,
         QStringLiteral("Fill the first empty zone when a new window opens. When on, this overrides each layout's "
                        "individual auto-assign toggle and applies to every layout.")},
        {CD::suppressDefaultLayoutAssignmentKey(), CD::suppressDefaultLayoutAssignment(), QMetaType::Bool,
         QStringLiteral("Snapping and tiling stay off until you assign a layout. A rule can re-enable the default per "
                        "monitor.")},
        {CD::defaultLayoutIdKey(), CD::defaultLayoutId(), QMetaType::QString,
         QStringLiteral("Layout a screen uses until it is given one of its own. Empty picks the first layout that fits "
                        "the screen.")},
    };
    schema.groups[CD::snappingBehaviorSnapAssistGroup()] = {
        {CD::featureEnabledKey(), CD::snapAssistFeatureEnabled(), QMetaType::Bool,
         QStringLiteral("Offer to fill the remaining empty zones after you snap a window.")},
        {CD::enabledKey(), CD::snapAssistEnabled(), QMetaType::Bool,
         QStringLiteral("Show the window picker after every snap, without waiting for you to hold anything.")},
        {CD::triggersKey(), CD::snapAssistTriggers(), QMetaType::QVariantList,
         QStringLiteral("Modifier and mouse-button combinations that bring up the snap-assist picker for a single "
                        "snap. Each entry is a {modifier, mouseButton} pair."),
         canonicalTriggerList},
        {CD::releaseGraceMsKey(), CD::snapAssistGraceMs(), QMetaType::Int,
         QStringLiteral("How long the snap-assist picker stays up after the trigger is released."),
         clampInt(CD::triggerGraceMsMin(), CD::triggerGraceMsMax())},
    };
}

// ─── Windows (window decoration appearance) ─────────────────────────────────
// Mode-neutral window border + title bar. Border colours are theme-fallback
// strings (EMPTY = "follow the system accent", or a hex/named colour),
// validated by canonicalThemeFallbackColor like every other follow-the-system
// colour key. The border/title-bar scope is a closed-set token
// ("tiled" / "normal" / "all") the Appearance page and the effect agree on,
// snapped to the default on an unknown on-disk value.
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
        {CD::showBorderKey(), CD::showWindowBorder(), QMetaType::Bool,
         QStringLiteral("Draw a coloured border around windows placed by PlasmaZones.")},
        {CD::borderScopeKey(), CD::windowBorderScope(), QMetaType::QString,
         QStringLiteral("Which windows get a border."), scopeValidator(CD::windowBorderScope()),
         tokenChoices({WAS::Tiled, WAS::Normal, WAS::All})},
        {CD::widthKey(), CD::windowBorderWidth(), QMetaType::Int,
         QStringLiteral("Thickness of the coloured border around windows."),
         clampInt(CD::windowBorderWidthMin(), CD::windowBorderWidthMax())},
        {CD::radiusKey(), CD::windowBorderRadius(), QMetaType::Int,
         QStringLiteral("Roundness of the border corners. Zero is square."),
         clampInt(CD::windowBorderRadiusMin(), CD::windowBorderRadiusMax())},
        {CD::borderColorActiveKey(), CD::windowBorderColorActive(), QMetaType::QString,
         QStringLiteral("Border colour for the focused window. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        {CD::borderColorInactiveKey(), CD::windowBorderColorInactive(), QMetaType::QString,
         QStringLiteral("Border colour for unfocused windows. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
        {CD::hideTitleBarsKey(), CD::hideWindowTitleBars(), QMetaType::Bool,
         QStringLiteral("Remove window title bars, restored when a window floats.")},
        {CD::titleBarScopeKey(), CD::windowTitleBarScope(), QMetaType::QString,
         QStringLiteral("Which windows lose their title bar."), scopeValidator(CD::windowTitleBarScope()),
         tokenChoices({WAS::Tiled, WAS::Normal, WAS::All})},
        {CD::focusFadeDurationKey(), CD::focusFadeDuration(), QMetaType::Int,
         QStringLiteral("How long decorations take to fade between focused and unfocused. Zero switches instantly."),
         clampInt(CD::focusFadeDurationMin(), CD::focusFadeDurationMax())},
        // Plain opacity+tint layer: opacity/strength are [0.0, 1.0] doubles,
        // the tint colour shares the border-colour shape (#AARRGGBB or the
        // empty follow-the-accent sentinel) and the scope shares the closed
        // token set.
        {CD::showOpacityTintKey(), CD::showWindowOpacityTint(), QMetaType::Bool,
         QStringLiteral("Fade and tint windows placed by PlasmaZones.")},
        {CD::opacityTintScopeKey(), CD::windowOpacityTintScope(), QMetaType::QString,
         QStringLiteral("Which windows are faded and tinted."), scopeValidator(CD::windowOpacityTintScope()),
         tokenChoices({WAS::Tiled, WAS::Normal, WAS::All})},
        {CD::opacityKey(), CD::windowOpacity(), QMetaType::Double,
         QStringLiteral("How visible matched windows stay, where 1 is fully opaque."),
         clampDouble(CD::windowOpacityMin(), CD::windowOpacityMax())},
        {CD::tintStrengthKey(), CD::windowTintStrength(), QMetaType::Double,
         QStringLiteral("How strongly the tint colour blends over the window, where 0 keeps it untinted."),
         clampDouble(CD::windowTintStrengthMin(), CD::windowTintStrengthMax())},
        {CD::tintColorKey(), CD::windowTintColor(), QMetaType::QString,
         QStringLiteral("Colour blended over matched windows. Empty follows the colour scheme."),
         canonicalThemeFallbackColor},
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
        {CD::innerGapKey(), CD::innerGap(), QMetaType::Int, QStringLiteral("Space between neighbouring windows."),
         clampInt(CD::innerGapMin(), CD::innerGapMax())},
        {CD::outerGapKey(), CD::outerGap(), QMetaType::Int,
         QStringLiteral("Space between the windows and the edges of the screen. Ignored when per-side outer gaps are "
                        "on."),
         clampInt(CD::outerGapMin(), CD::outerGapMax())},
        {CD::usePerSideOuterGapKey(), CD::usePerSideOuterGap(), QMetaType::Bool,
         QStringLiteral("Set a different outer gap for each screen edge instead of one value for all four.")},
        {CD::outerGapTopKey(), CD::outerGapTop(), QMetaType::Int,
         QStringLiteral("Outer gap along the top edge of the screen. Only applies when per-side outer gaps are on."),
         clampInt(CD::outerGapTopMin(), CD::outerGapTopMax())},
        {CD::outerGapBottomKey(), CD::outerGapBottom(), QMetaType::Int,
         QStringLiteral("Outer gap along the bottom edge of the screen. Only applies when per-side outer gaps are on."),
         clampInt(CD::outerGapBottomMin(), CD::outerGapBottomMax())},
        {CD::outerGapLeftKey(), CD::outerGapLeft(), QMetaType::Int,
         QStringLiteral("Outer gap along the left edge of the screen. Only applies when per-side outer gaps are on."),
         clampInt(CD::outerGapLeftMin(), CD::outerGapLeftMax())},
        {CD::outerGapRightKey(), CD::outerGapRight(), QMetaType::Int,
         QStringLiteral("Outer gap along the right edge of the screen. Only applies when per-side outer gaps are on."),
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
         QMetaType::QVariantMap,
         QStringLiteral("The decoration profiles themselves, as a baseline set plus per-window overrides. The "
                        "decorations page writes this, so it is not meant to be edited by hand.")},
    };
    // Mostly what the decoration chain is allowed to keep redrawing (an animated
    // pack repaints every window carrying it on every vsync, which never lets the
    // GPU leave its top performance state), plus one per-frame-cost knob, the
    // blur-scale multiplier.
    schema.groups[CD::decorationsPerformanceGroup()] = {
        {CD::animateFocusedOnlyKey(), CD::decorationAnimateFocusedOnly(), QMetaType::Bool,
         QStringLiteral("Run decoration animations only on the focused window. Unfocused windows keep their decoration "
                        "but hold still.")},
        {CD::pauseWhenIdleKey(), CD::decorationPauseWhenIdle(), QMetaType::Bool,
         QStringLiteral("Stop decoration animations while you are not interacting, and resume on the next input.")},
        // Clamped here, not in the UI. P_STORE_SET_INT delegates range enforcement
        // to the schema validator, and the daemon feeds this straight into an
        // ext-idle-notify-v1 timeout as `value * 1000` — a hand-edited 0 or -1 in
        // config.json would otherwise arm a nonsensical timer (fire-immediately, or
        // rejected outright, so the pause never engages). The slider's from/to are a
        // UI affordance, not a validation boundary.
        {CD::idleTimeoutSecKey(), CD::decorationIdleTimeoutSec(), QMetaType::Int,
         QStringLiteral("How long without input before decoration animations pause. Only applies when pausing while "
                        "idle is on."),
         clampInt(CD::decorationIdleTimeoutSecMin(), CD::decorationIdleTimeoutSecMax())},
        // Multiplier on each pack's declared buffer-pass resolution (the blur
        // pyramid density). Clamped so the persisted value stays inside its
        // declared band and every surface agrees on it: without this a
        // hand-edited 5.0 would ride the wire as 5.0 and render as "500%" in
        // the profile diff. Defence in depth rather than the only guard — the
        // effect's own loader independently rejects non-numeric, non-finite,
        // and non-positive replies, and clamps to the same shared bounds. The
        // band is deliberately wider on the low end than the UI's three tiers
        // (headroom down to 0.25 for hand edits); the combo highlights the
        // nearest tier for an off-tier value.
        {CD::blurScaleMultiplierKey(), CD::decorationBlurScaleMultiplier(), QMetaType::Double,
         QStringLiteral("Resolution the blur passes render at, relative to the window. Below 1 is cheaper and softer, "
                        "above 1 is sharper and costs more."),
         clampDouble(CD::decorationBlurScaleMultiplierMin(), CD::decorationBlurScaleMultiplierMax())},
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
