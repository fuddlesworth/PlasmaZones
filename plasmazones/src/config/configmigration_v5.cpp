// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
#include "configkeys.h"
#include "perscreenresolver.h"
#include "settings.h"
#include "configmigration_util.h"

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

// ── v4 → v5: per-mode appearance + gaps → unified config groups ─────────────
//
// The v4 schema stored per-mode (separate Snapping vs Tiling) window
// appearance (borders, title bars, colours) and gap settings in config.json
// PLUS per-screen gap subsets. v5 unifies the per-mode global values into two
// config groups that apply to both modes: "Windows" (appearance) and "Gaps".
// migrateV4ToV5 collapses the two per-mode value sets into one and writes the
// values that DIFFER from the v4 compile defaults into those groups; it creates
// NO rules. The per-screen gap subsets are likewise collapsed per monitor and
// folded into the per-screen autotile gap keys (consumeV4PerScreenGaps).
//
// Schema-migration freeze policy (mirrors migrateV3ToV4): every v4 group/key
// spelling and every v4 compile-default value the migration depends on is
// frozen here as a file-scope constant. The live ConfigDefaults accessors for
// these settings were DELETED on this branch, and the underlying library
// constants (DecorationDefaults, core gap constants) could in principle drift;
// pinning the literals decouples this migration's stable wire-format contract
// from the live code.
//
// The v5 DESTINATION keys (the unified "Windows" / "Gaps" groups this step writes
// into) are deliberately NOT frozen here — they are written through the live
// ConfigKeys accessors because v5 is the current schema. A future v5→v6 step that
// renames those accessors MUST freeze them as constants first, or this historical
// step will silently retarget to the renamed groups.
namespace {

// ── Frozen v4 on-disk group paths (dot-paths) and leaf-key spellings ───────
// Global per-mode appearance lives under "<Mode>.Appearance.{Colors,
// Decorations,Borders}" and the gaps under "<Mode>.Gaps".
constexpr QLatin1String kV4ModeSnapping{"Snapping"};
constexpr QLatin1String kV4ModeTiling{"Tiling"};
constexpr QLatin1String kV4SegAppearance{"Appearance"};
constexpr QLatin1String kV4SegColors{"Colors"};
constexpr QLatin1String kV4SegDecorations{"Decorations"};
constexpr QLatin1String kV4SegBorders{"Borders"};
constexpr QLatin1String kV4SegGaps{"Gaps"};

// Flat audio-spectrum keys under "Shaders" — v5 moves them into the
// "Shaders.Audio" group (Enabled / Bars).
constexpr QLatin1String kV4KeyAudioVisualizer{"AudioVisualizer"};
constexpr QLatin1String kV4KeyAudioSpectrumBarCount{"AudioSpectrumBarCount"};

// Appearance leaf keys.
constexpr QLatin1String kV4KeyActive{"Active"};
constexpr QLatin1String kV4KeyInactive{"Inactive"};
constexpr QLatin1String kV4KeyUseSystem{"UseSystem"};
constexpr QLatin1String kV4KeyHideTitleBars{"HideTitleBars"};
constexpr QLatin1String kV4KeyShowBorder{"ShowBorder"};
constexpr QLatin1String kV4KeyWidth{"Width"};
constexpr QLatin1String kV4KeyRadius{"Radius"};

// Global gap leaf keys.
constexpr QLatin1String kV4KeyInner{"Inner"};
constexpr QLatin1String kV4KeyOuter{"Outer"};
constexpr QLatin1String kV4KeyUsePerSide{"UsePerSide"};
constexpr QLatin1String kV4KeyTop{"Top"};
constexpr QLatin1String kV4KeyBottom{"Bottom"};
constexpr QLatin1String kV4KeyLeft{"Left"};
constexpr QLatin1String kV4KeyRight{"Right"};

// v4 persisted per-screen gaps under "PerScreen/{Snapping,Autotile}/<screenId>"
// (the nested container PerScreenPathResolver maps the "SnappingScreen:" /
// "AutotileScreen:" store groups to). v5 unifies the two per-mode gap sets into
// ONE value per monitor, stored in the per-screen autotile group (which the
// config store still reads via "AutotileScreen:<id>" → PerScreen/Autotile/<id>).
// These frozen literals mirror the v4 container/category spellings and the v4
// gap-key spellings — snapping used bare key names (with the inner-gap
// dimension under its legacy "ZonePadding" spelling), autotile the
// "Autotile"-prefixed PerScreenAutotileKey names — so the consume step is
// decoupled from the live accessors (today's shared spellings live in
// PhosphorEngine::PerScreenKeys). v4 and v5 share the same PerScreen/Autotile
// mapping, so the unified values written here are read back by the current store.
constexpr QLatin1String kV4PerScreenContainer{"PerScreen"};
constexpr QLatin1String kV4PerScreenSnappingCat{"Snapping"};
constexpr QLatin1String kV4PerScreenAutotileCat{"Autotile"};
// The struct + collapse/consume helpers depend on the frozen kV4Def* gap
// defaults declared below, so they live just after those (before stripKeysAtPath).

// ── Frozen v4 compile defaults ─────────────────────────────────────────────
// Sourced from the (now-deleted) ConfigDefaults accessors:
//   ShowBorder/HideTitleBars/BorderWidth/BorderRadius → DecorationDefaults,
//   UseSystemColors → true, inner/outer gaps → core ZonePadding/OuterGap (8).
constexpr bool kV4DefShowBorder = false;
constexpr int kV4DefBorderWidth = 2;
constexpr int kV4DefBorderRadius = 8;
constexpr bool kV4DefHideTitleBars = false;
constexpr bool kV4DefUseSystemColors = true;
constexpr int kV4DefInnerGap = 8;
constexpr int kV4DefOuterGap = 8;
constexpr bool kV4DefUsePerSideOuterGap = false;

// ── Normalized field names for the collapsed per-mode value set ────────────
constexpr QLatin1String kFieldShowBorder{"showBorder"};
constexpr QLatin1String kFieldBorderWidth{"borderWidth"};
constexpr QLatin1String kFieldBorderRadius{"borderRadius"};
constexpr QLatin1String kFieldHideTitleBars{"hideTitleBars"};
constexpr QLatin1String kFieldActiveColor{"activeColor"};
constexpr QLatin1String kFieldInactiveColor{"inactiveColor"};
constexpr QLatin1String kFieldInnerGap{"innerGap"};
constexpr QLatin1String kFieldOuterGap{"outerGap"};
constexpr QLatin1String kFieldUsePerSideOuterGap{"usePerSideOuterGap"};
constexpr QLatin1String kFieldOuterGapTop{"outerGapTop"};
constexpr QLatin1String kFieldOuterGapBottom{"outerGapBottom"};
constexpr QLatin1String kFieldOuterGapLeft{"outerGapLeft"};
constexpr QLatin1String kFieldOuterGapRight{"outerGapRight"};

// ── migrate-side gating helpers ────────────────────────────────────────────
// Insert the normalized field into @p out only when the source key is present
// AND its value differs from the v4 compile default.
void stashIntIfDiffers(const QJsonObject& grp, QLatin1String key, int def, QJsonObject& out, QLatin1String field)
{
    const QJsonValue v = grp.value(key);
    if (v.isDouble() && v.toInt() != def) {
        out.insert(field, v.toInt());
    }
}

void stashBoolIfDiffers(const QJsonObject& grp, QLatin1String key, bool def, QJsonObject& out, QLatin1String field)
{
    const QJsonValue v = grp.value(key);
    if (v.isBool() && v.toBool() != def) {
        out.insert(field, v.toBool());
    }
}

// Read a stored colour value (written by JsonBackend as a "#AARRGGBB" string)
// and, when valid, insert the normalized #AARRGGBB hex into @p out.
void stashColor(const QJsonObject& colors, QLatin1String key, QJsonObject& out, QLatin1String field)
{
    const QJsonValue v = colors.value(key);
    if (!v.isString()) {
        return;
    }
    const QColor c(v.toString());
    if (c.isValid()) {
        out.insert(field, c.name(QColor::HexArgb));
    }
}

// Read one global mode's ("Snapping" or "Tiling") appearance + gap values into
// a field-keyed object, keeping ONLY the fields that DIFFER from the v4 compile
// default (an absent field means "still at default"). Colours are read only
// when the user turned the system-accent colours OFF (the v4 default is
// accent-on, which equals the v5 "accent" config default, so it contributes
// nothing).
QJsonObject buildModeStash(const QJsonObject& root, QLatin1String mode)
{
    const QString modeStr = QString(mode);
    // Build the v4 group paths from the frozen segment constants (same spellings
    // the strip side uses) so a rename can't desync the two sides.
    const QString appearance = modeStr + QLatin1Char('.') + kV4SegAppearance + QLatin1Char('.');
    const QJsonObject colors = groupObjectAtPath(root, appearance + kV4SegColors);
    const QJsonObject deco = groupObjectAtPath(root, appearance + kV4SegDecorations);
    const QJsonObject borders = groupObjectAtPath(root, appearance + kV4SegBorders);
    const QJsonObject gaps = groupObjectAtPath(root, modeStr + QLatin1Char('.') + kV4SegGaps);

    QJsonObject out;
    stashBoolIfDiffers(borders, kV4KeyShowBorder, kV4DefShowBorder, out, kFieldShowBorder);
    stashIntIfDiffers(borders, kV4KeyWidth, kV4DefBorderWidth, out, kFieldBorderWidth);
    stashIntIfDiffers(borders, kV4KeyRadius, kV4DefBorderRadius, out, kFieldBorderRadius);
    stashBoolIfDiffers(deco, kV4KeyHideTitleBars, kV4DefHideTitleBars, out, kFieldHideTitleBars);

    const QJsonValue useSystem = colors.value(kV4KeyUseSystem);
    const bool systemOn = useSystem.isBool() ? useSystem.toBool() : kV4DefUseSystemColors;
    if (!systemOn) {
        stashColor(colors, kV4KeyActive, out, kFieldActiveColor);
        stashColor(colors, kV4KeyInactive, out, kFieldInactiveColor);
    }

    stashIntIfDiffers(gaps, kV4KeyInner, kV4DefInnerGap, out, kFieldInnerGap);
    stashIntIfDiffers(gaps, kV4KeyOuter, kV4DefOuterGap, out, kFieldOuterGap);
    stashBoolIfDiffers(gaps, kV4KeyUsePerSide, kV4DefUsePerSideOuterGap, out, kFieldUsePerSideOuterGap);
    stashIntIfDiffers(gaps, kV4KeyTop, kV4DefOuterGap, out, kFieldOuterGapTop);
    stashIntIfDiffers(gaps, kV4KeyBottom, kV4DefOuterGap, out, kFieldOuterGapBottom);
    stashIntIfDiffers(gaps, kV4KeyLeft, kV4DefOuterGap, out, kFieldOuterGapLeft);
    stashIntIfDiffers(gaps, kV4KeyRight, kV4DefOuterGap, out, kFieldOuterGapRight);
    return out;
}

// Strip the named keys from the group at @p segments, pruning the group (and
// now-empty ancestors) if nothing else remains. Keys not consumed here (e.g.
// Snapping.Gaps.AdjacentThreshold, Tiling.Gaps.SmartGaps) survive.
void stripKeysAtPath(QJsonObject& root, const QStringList& segments, const QStringList& keys)
{
    QJsonObject grp = groupObjectAtPath(root, segments.join(QLatin1Char('.')));
    if (grp.isEmpty()) {
        return;
    }
    for (const QString& key : keys) {
        grp.remove(key);
    }
    if (grp.isEmpty()) {
        removeGroupAtSegments(root, segments);
    } else {
        setGroupAtSegments(root, segments, grp);
    }
}

// One gap dimension's v4 per-screen key spellings + compile default. `snapKey`
// is the bare snapping form, `autotileKey` the "Autotile"-prefixed form (also the
// v5 destination key), `isBool` selects bool vs int, `def` is the v4 compile
// default a value must differ from to be carried.
struct V4PerScreenGapDim
{
    QLatin1String snapKey;
    QLatin1String autotileKey;
    bool isBool;
    int def; // int default; for bool dims 0/1 mirrors kV4DefUsePerSideOuterGap
};

constexpr V4PerScreenGapDim kV4PerScreenGapDims[] = {
    // The v4 per-screen SNAPPING inner-gap key is the legacy "ZonePadding"
    // spelling (renamed to "InnerGap" in this refactor); the autotile side always
    // used the "Autotile"-prefixed name, which is also the v5 destination key.
    {QLatin1String{"ZonePadding"}, QLatin1String{"AutotileInnerGap"}, false, kV4DefInnerGap},
    {QLatin1String{"OuterGap"}, QLatin1String{"AutotileOuterGap"}, false, kV4DefOuterGap},
    {QLatin1String{"UsePerSideOuterGap"}, QLatin1String{"AutotileUsePerSideOuterGap"}, true,
     kV4DefUsePerSideOuterGap ? 1 : 0},
    {QLatin1String{"OuterGapTop"}, QLatin1String{"AutotileOuterGapTop"}, false, kV4DefOuterGap},
    {QLatin1String{"OuterGapBottom"}, QLatin1String{"AutotileOuterGapBottom"}, false, kV4DefOuterGap},
    {QLatin1String{"OuterGapLeft"}, QLatin1String{"AutotileOuterGapLeft"}, false, kV4DefOuterGap},
    {QLatin1String{"OuterGapRight"}, QLatin1String{"AutotileOuterGapRight"}, false, kV4DefOuterGap},
};

// Collapse a single gap dimension's per-mode (snapping + autotile) v4 values into
// one, keeping ONLY a value that DIFFERS from the compile default. Prefer the
// value that differs; on a tie (both differ, both default, or both absent) prefer
// SNAPPING. Returns nullopt when neither mode carries a differing value, so the
// caller drops the key and the monitor falls back to the global gap on read.
std::optional<QJsonValue> collapseV4PerScreenGap(const QJsonValue& snap, const QJsonValue& autotile,
                                                 const V4PerScreenGapDim& dim)
{
    const auto present = [&](const QJsonValue& v) {
        return dim.isBool ? v.isBool() : v.isDouble();
    };
    const auto differs = [&](const QJsonValue& v) {
        return dim.isBool ? (v.toBool() != (dim.def != 0)) : (v.toInt() != dim.def);
    };
    if (present(snap) && differs(snap)) {
        return snap;
    }
    if (present(autotile) && differs(autotile)) {
        return autotile;
    }
    return std::nullopt;
}

// Fold the v4 per-screen gap subsets (PerScreen/Snapping + PerScreen/Autotile)
// into the unified per-screen autotile gap keys, stripping the consumed gap keys
// from each side. The Autotile category keeps its non-gap keys (algorithm/
// behaviour) and gains the collapsed gap dimensions; the Snapping category keeps
// any non-gap per-screen keys (only its gap keys are removed). A collapsed value
// equal to the default is not written (and any pre-existing default-valued gap
// key is removed) so the store's differ-from-default contract holds. Categories /
// screens that become empty are pruned.
void consumeV4PerScreenGaps(QJsonObject& root)
{
    QJsonObject perScreen = root.value(kV4PerScreenContainer).toObject();
    if (perScreen.isEmpty()) {
        return;
    }
    QJsonObject snappingCat = perScreen.value(kV4PerScreenSnappingCat).toObject();
    QJsonObject autotileCat = perScreen.value(kV4PerScreenAutotileCat).toObject();

    QSet<QString> screenIds;
    for (const QString& id : snappingCat.keys()) {
        screenIds.insert(id);
    }
    for (const QString& id : autotileCat.keys()) {
        screenIds.insert(id);
    }

    for (const QString& id : screenIds) {
        QJsonObject snapScreen = snappingCat.value(id).toObject();
        QJsonObject autotileScreen = autotileCat.value(id).toObject();
        for (const V4PerScreenGapDim& dim : kV4PerScreenGapDims) {
            const std::optional<QJsonValue> collapsed =
                collapseV4PerScreenGap(snapScreen.value(dim.snapKey), autotileScreen.value(dim.autotileKey), dim);
            if (collapsed) {
                autotileScreen[dim.autotileKey] = *collapsed;
            } else {
                autotileScreen.remove(dim.autotileKey);
            }
            // Strip only the consumed snapping-side gap key; any non-gap
            // per-screen snapping key (e.g. a per-screen snap-assist toggle) stays.
            snapScreen.remove(dim.snapKey);
        }
        if (autotileScreen.isEmpty()) {
            autotileCat.remove(id);
        } else {
            autotileCat[id] = autotileScreen;
        }
        if (snapScreen.isEmpty()) {
            snappingCat.remove(id);
        } else {
            snappingCat[id] = snapScreen;
        }
    }

    // Write back the categories, pruning any that became empty once their gap
    // keys were consumed.
    if (snappingCat.isEmpty()) {
        perScreen.remove(kV4PerScreenSnappingCat);
    } else {
        perScreen[kV4PerScreenSnappingCat] = snappingCat;
    }
    if (autotileCat.isEmpty()) {
        perScreen.remove(kV4PerScreenAutotileCat);
    } else {
        perScreen[kV4PerScreenAutotileCat] = autotileCat;
    }
    if (perScreen.isEmpty()) {
        root.remove(kV4PerScreenContainer);
    } else {
        root[kV4PerScreenContainer] = perScreen;
    }
}

} // namespace

void ConfigMigration::migrateV4ToV5(QJsonObject& root)
{
    // Defense-in-depth idempotency guard, mirroring the earlier steps.
    if (root.value(ConfigKeys::versionKey()).toInt(0) >= 5) {
        return;
    }

    // ── Collapse the two per-mode value sets into one ──────────────────────
    // v4 stored appearance (borders / title bars / colours) and gaps
    // SEPARATELY for Snapping and Tiling. v5 unifies them into the "Windows"
    // and "Gaps" config groups that apply to BOTH modes. buildModeStash returns
    // only the fields that DIFFER from the v4 compile default, so a present
    // field is a genuine user override.
    //
    // Collapse rule, per field: prefer the value that DIFFERS from the compile
    // default; when both modes differ (or both are at default) prefer the
    // SNAPPING value. Because buildModeStash omits default fields, this reduces
    // to "tiling as the base, snapping overriding on top": a snapping override
    // wins whenever present, else a tiling override is taken, else the field
    // stays at its default (unwritten).
    const QJsonObject snappingValues = buildModeStash(root, kV4ModeSnapping);
    const QJsonObject tilingValues = buildModeStash(root, kV4ModeTiling);
    QJsonObject unified = tilingValues;
    for (auto it = snappingValues.constBegin(); it != snappingValues.constEnd(); ++it) {
        unified.insert(it.key(), it.value());
    }

    // ── Write the collapsed values into the unified config groups ──────────
    // Only differing-from-default fields are present in `unified`, so every
    // field it carries is written verbatim; absent fields fall back to the
    // config default on read (same differ-from-default contract the v4 read
    // side applies). borderScope / titleBarScope had no per-scope data in v4,
    // so they are left unwritten and take their "tiled" config default.
    const auto carry = [&unified](QJsonObject& grp, QLatin1String field, const QString& key) {
        const QJsonValue v = unified.value(field);
        if (!v.isUndefined()) {
            grp.insert(key, v);
        }
    };

    QJsonObject windows = groupObjectAtPath(root, ConfigKeys::windowsAppearanceGroup());
    carry(windows, kFieldShowBorder, ConfigKeys::showBorderKey());
    carry(windows, kFieldBorderWidth, ConfigKeys::widthKey());
    carry(windows, kFieldBorderRadius, ConfigKeys::radiusKey());
    carry(windows, kFieldHideTitleBars, ConfigKeys::hideTitleBarsKey());
    carry(windows, kFieldActiveColor, ConfigKeys::borderColorActiveKey());
    carry(windows, kFieldInactiveColor, ConfigKeys::borderColorInactiveKey());
    if (!windows.isEmpty()) {
        setGroupAtSegments(root, {ConfigKeys::windowsAppearanceGroup()}, windows);
    }

    QJsonObject gaps = groupObjectAtPath(root, ConfigKeys::gapsGroup());
    carry(gaps, kFieldInnerGap, ConfigKeys::innerGapKey());
    carry(gaps, kFieldOuterGap, ConfigKeys::outerGapKey());
    carry(gaps, kFieldUsePerSideOuterGap, ConfigKeys::usePerSideOuterGapKey());
    carry(gaps, kFieldOuterGapTop, ConfigKeys::outerGapTopKey());
    carry(gaps, kFieldOuterGapBottom, ConfigKeys::outerGapBottomKey());
    carry(gaps, kFieldOuterGapLeft, ConfigKeys::outerGapLeftKey());
    carry(gaps, kFieldOuterGapRight, ConfigKeys::outerGapRightKey());
    if (!gaps.isEmpty()) {
        setGroupAtSegments(root, {ConfigKeys::gapsGroup()}, gaps);
    }

    // Fold the v4 per-screen gap subsets (PerScreen/{Snapping,Autotile}/<screen>)
    // into the unified per-screen autotile gap keys and drop the consumed
    // PerScreen/Snapping subtree.
    consumeV4PerScreenGaps(root);

    // ── Remove the consumed v4 global appearance / gap keys ────────────────
    // The per-mode appearance sub-groups are fully consumed — drop the three
    // leaf sub-groups and let removeGroupAtSegments prune the now-empty
    // Appearance parent. Gaps keep their surviving non-gap keys (AdjacentThreshold
    // / SmartGaps), so strip only the consumed gap keys.
    for (QLatin1String mode : {kV4ModeSnapping, kV4ModeTiling}) {
        removeGroupAtSegments(root, {QString(mode), QString(kV4SegAppearance), QString(kV4SegColors)});
        removeGroupAtSegments(root, {QString(mode), QString(kV4SegAppearance), QString(kV4SegDecorations)});
        removeGroupAtSegments(root, {QString(mode), QString(kV4SegAppearance), QString(kV4SegBorders)});
        stripKeysAtPath(root, {QString(mode), QString(kV4SegGaps)},
                        {kV4KeyInner, kV4KeyOuter, kV4KeyUsePerSide, kV4KeyTop, kV4KeyBottom, kV4KeyLeft, kV4KeyRight});
    }

    // ── Drop the retired overlay-shader master toggle ───────────────────────
    // v5 removes Shaders.Enabled entirely: shader use is decided per layout
    // (shaderId "none" draws the rectangle overlay), so the global switch
    // gated nothing the layouts don't already control. The key reaches this
    // strip from two sources: existing v2-v4 configs where the user set the
    // toggle, and v1 configs whose EnableShaderEffects the v1→v2 step still
    // renames to Shaders.Enabled. Stripping here keeps the chain's output
    // aligned with the v5 schema for both.
    stripKeysAtPath(root, {ConfigKeys::shadersGroup()}, {ConfigKeys::enabledKey()});

    // ── Move the flat audio keys into the Shaders.Audio group ──────────────
    // v5 grows a full audio-spectrum parameter group (Shaders.Audio). The two
    // audio keys that shipped under flat Shaders move there so user values
    // survive; the source spellings are frozen above (kV4KeyAudio*), the
    // destinations are written through the live accessors (v5 is current).
    {
        const QJsonObject shaders = groupObjectAtPath(root, ConfigKeys::shadersGroup());
        QJsonObject audio;
        const QJsonValue viz = shaders.value(kV4KeyAudioVisualizer);
        if (viz.isBool()) {
            audio.insert(ConfigKeys::enabledKey(), viz);
        }
        const QJsonValue bars = shaders.value(kV4KeyAudioSpectrumBarCount);
        if (bars.isDouble()) {
            audio.insert(ConfigKeys::barsKey(), bars);
        }
        if (!audio.isEmpty()) {
            setGroupAtSegments(root, ConfigKeys::shadersAudioGroup().split(QLatin1Char('.')), audio);
        }
        stripKeysAtPath(root, {ConfigKeys::shadersGroup()}, {kV4KeyAudioVisualizer, kV4KeyAudioSpectrumBarCount});
    }

    // Stamp literal 5 — see migrateV1ToV2 for why this isn't ConfigSchemaVersion.
    root[ConfigKeys::versionKey()] = 5;
}

} // namespace PlasmaZones
