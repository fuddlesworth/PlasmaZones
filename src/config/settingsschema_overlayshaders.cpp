// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The zone-overlay shader half of the settings schema: the single
// Overlays group and its OverlayShaderTree blob. Split out of
// settingsschema.cpp for file-size the way the scrolling and tiling TUs were;
// the entry point (appendOverlayShadersSchema) is declared alongside every
// other appendXxxSchema in settingsschema.h. sanitizeOverlayShaderTree stays
// file-local: the overlay tree is the one key that round-trips through
// OverlayShaderTree, so this is its only consumer.

#include "settingsschema.h"
#include "settingsschema_shaderbounds.h"

#include "configdefaults.h"
#include "core/types/overlayshadertree.h"

#include <QJsonObject>
#include <QStringList>
#include <QUuid>

namespace PlasmaZones {

namespace {

/// A shaderId, a parameter name, and a colour or image-path parameter value
/// are all short. The caps themselves live in settingsschema_shaderbounds.h, shared
/// with the animation and decoration sanitizers — this file used to keep its own
/// copies of the same three numbers plus its own over-long-string predicate and
/// parameter loop, which is the duplication the shared header exists to remove. The
/// numbers agreed, which is the only reason the duplication was invisible.
constexpr int kMaxStringChars = kMaxShaderStringChars;
constexpr int kMaxParameters = kMaxShaderParameters;
constexpr int kMaxOverrides = kMaxShaderOverrides;

OverlayShaderProfile boundedProfile(const OverlayShaderProfile& profile)
{
    OverlayShaderProfile out;
    // Each field is bounded independently on purpose. Dropping the surviving
    // parameters along with an over-long id would be a second, coupled rule for
    // no gain: as a baseline an empty id is simply the unset default and the
    // leftover values are inert, and if the id is later corrected by hand the
    // parameters are still there. tests/unit/config/settings/
    // test_settings_overlay_shader_tree.cpp pins this.
    if (profile.shaderId.size() <= kMaxStringChars)
        out.shaderId = profile.shaderId;
    // Same independent bound. An over-long preset id is dropped and the
    // assignment falls back to its own parameters, which is exactly what an
    // id naming no preset already resolves to.
    if (profile.presetId.size() <= kMaxStringChars)
        out.presetId = profile.presetId;
    // The shared bounder, not a fourth copy of the same loop: over-long keys and
    // values dropped, non-scalar values dropped, the map capped.
    out.parameters = boundedShaderParams(profile.parameters);
    return out;
}

/// Canonicalize the overlay shader tree: round-trip through
/// @c OverlayShaderTree so unknown fields are dropped, then bound the parts
/// that round trip survives verbatim — string lengths, the parameter map's
/// size and value shapes, and the count and spelling of override keys.
///
/// This is a PERSISTENCE boundary, not a UI convenience, and it sits on the
/// schema key rather than in @c Settings::setOverlayShaderTree because the
/// typed setter is not the only door. A settings profile being applied goes
/// through @c Store::importFromJson, which writes the blob's value for every
/// declared key straight into the store; @c config.json is hand-editable; and
/// a validator runs on read as well as write, so this covers both without a
/// second copy at each writer.
///
/// Override paths are layout UUIDs in the braced @c QUuid::toString() form the
/// project uses everywhere. A non-UUID key is refused outright (the
/// `autotile:<algoId>` entries the pre-v8 editor could stamp), and a key that
/// parses but is spelled without braces is NORMALIZED rather than kept:
/// @c QUuid::fromString accepts both forms while every reader asks with
/// @c QUuid::toString(), so an unbraced key would sit in the tree matching
/// nothing. Normalizing here covers the migration's writer too, which reaches
/// @c config.json outside the store, because the value passes this validator on
/// the first read back. Note a config holding BOTH spellings of one layout
/// collapses to a single entry, the braced one winning by sort order.
///
/// Deliberately does NOT judge a shaderId against the installed packs, or a
/// parameter against the type its pack declares. Both would need a
/// ShaderRegistry this layer must not grow, the same argument the motion
/// tree's setter makes about CurveRegistry, and an assignment naming a pack
/// the user is about to install must not be erased for being early.
///
/// Idempotent by construction: a projection onto the two declared fields
/// composed with bounds that are stable under reapplication, over
/// @c overriddenLayouts()'s sorted order so the same entries survive a second
/// pass.
QVariant sanitizeOverlayShaderTree(const QVariant& v)
{
    const OverlayShaderTree in = OverlayShaderTree::fromJson(QJsonObject::fromVariantMap(v.toMap()));
    OverlayShaderTree out;
    out.setBaseline(boundedProfile(in.baseline()));
    int kept = 0;
    const QStringList layouts = in.overriddenLayouts();
    for (const QString& layoutId : layouts) {
        if (kept >= kMaxOverrides)
            break;
        const QUuid parsed = QUuid::fromString(layoutId);
        if (parsed.isNull())
            continue;
        // Read on the original spelling, write on the canonical one.
        out.setOverride(parsed.toString(), boundedProfile(in.directOverride(layoutId)));
        ++kept;
    }
    return QVariant(out.toJson().toVariantMap());
}

} // namespace

// ─── Overlays ────────────────────────────────────────────────
// Zone-overlay shader assignments — one nested JSON blob (baseline +
// per-layout overrides), persisted as a QVariantMap like the animation
// ShaderProfileTree entry, which is sanitized the same way — both types round-trip
// with no registry. (The CurveRegistry argument that used to appear here belongs to
// the MOTION tree, not the animation shader tree, and is not what stops that one
// being covered; see settingsschema_shadertrees.cpp.)
void appendOverlayShadersSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::overlaysGroup()] = {
        {CD::overlayShaderTreeKey(), CD::overlayShaderTree(), QMetaType::QVariantMap,
         QStringLiteral(
             "Zone-overlay shader assignments (global baseline plus per-layout overrides). The "
             "settings app's Appearance → Overlays → Layouts page writes this, so it is not meant to be edited "
             "by hand."),
         sanitizeOverlayShaderTree},
    };
}

} // namespace PlasmaZones
