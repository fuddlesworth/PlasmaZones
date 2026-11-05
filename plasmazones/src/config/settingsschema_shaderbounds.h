// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @file
 * @brief Shared persistence bounds for the three shader-assignment trees.
 *
 * The zone-overlay tree grew these bounds first, and for a while it was the only
 * one that had them: the DECORATION tree bounded its writes in its typed setter
 * instead, and the animation shader tree bounded size nowhere at all. A
 * setter-side bound covers the settings UI and nothing else — not a settings
 * profile applied through `Store::importFromJson`, not a hand-edited
 * `config.json`, not a migration writing outside the store — which are exactly
 * the three doors the overlay sanitizer exists for. Three trees with the same
 * shape and three different answers is drift, so the bounds live here once and
 * every tree's sanitizer composes them, the overlay one included.
 *
 * What these do NOT do is judge CONTENT. A pack id is never checked against
 * the installed packs and a parameter is never checked against the type its
 * pack declares: both would need a registry this layer must not grow, and an
 * assignment naming a pack the user is about to install must not be erased for
 * being early. The bounds only stop an unbounded or wrong-SHAPED value from
 * reaching the key.
 *
 * Every helper here is idempotent, because a validator runs on read as well as
 * write and a second pass must produce the same bytes as the first.
 */

/// An id, a parameter name, and a colour or path parameter value are all short.
/// Well above anything a pack can legitimately declare, and far below the cost
/// of letting an unbounded string reach the key.
inline constexpr int kMaxShaderStringChars = 1024;

/// A pack's parameters are UBO lanes, and the metadata schemas cap a slot at 31
/// — 32 scalar lanes, 16 colour, 4 image. Comfortably above any pack that can
/// exist.
inline constexpr int kMaxShaderParameters = 64;

/// One override per assignable node. Above any plausible layout collection or
/// event/surface namespace; the point is that the count is bounded at all.
///
/// Only the ANIMATION tree can approach it, and only from a hand-edited file.
/// The decoration tree's keys are surface dot-paths from a fixed, build-time
/// namespace and `DecorationProfileTree::fromJson` drops every path this build
/// does not support, so its override count is capped by that namespace long
/// before this number. The cap is applied to both trees anyway rather than being
/// made animation-only: the cost is one comparison per override, and a bound
/// that exists on one of two structurally identical sanitizers is the kind of
/// asymmetry that reads as an oversight.
inline constexpr int kMaxShaderOverrides = 1024;

/// Entries in one decoration chain, and keys in one per-pack map.
///
/// 64 to match `kMaxDecorationListEntries` / `kMaxDecorationMapKeys`, the caps
/// `Settings::setDecorationProfileTree` has applied on the write path since
/// before this sanitizer existed. Deliberately the same number: a sanitizer
/// that trimmed harder than the setter would rewrite a tree the setter had
/// just accepted, so a value would vanish on the next read rather than at the
/// write that produced it.
inline constexpr int kMaxChainPacks = 64;

/// True when @p value is a string longer than `kMaxShaderStringChars`.
bool overLongShaderString(const QVariant& value);

/// @p in with over-long keys and values dropped, non-scalar values dropped, and
/// the whole map capped at `kMaxShaderParameters`.
///
/// Every parameter type a pack can declare is one scalar value: float, int,
/// bool, a colour string, an image path. A map or a list is nesting no pack
/// produces, so it can only have come from a hand-edit or a foreign writer.
QVariantMap boundedShaderParams(const QVariantMap& in);

/// Whether `boundedIdList` drops a repeated id.
enum class IdListDuplicates {
    Keep, ///< Leave repeats alone.
    Drop, ///< Keep the first occurrence of each id only.
};

/// @p in with over-long entries dropped and the list capped at @p maxCount.
/// Order is preserved, because a decoration chain folds in order.
///
/// Pass `IdListDuplicates::Drop` for a decoration chain. The count cap is not
/// the real resource there: the compositor folds per ENTRY with per-entry buffer
/// textures and FBO slots indexed by position, so 64 copies of one animated pack
/// costs 64 draws and 64 buffer slots every frame even though the shader is
/// compiled once.
QStringList boundedIdList(const QStringList& in, int maxCount, IdListDuplicates duplicates = IdListDuplicates::Keep);

/// @p in with over-long keys and non-string or over-long values dropped, and
/// the whole map capped at @p maxCount.
///
/// For the `{ packId -> presetId }` and similar id-to-id maps. A non-string
/// value is dropped rather than coerced: these are lookup keys, and an empty
/// string is the meaningful "none" answer rather than a repair.
QVariantMap boundedIdMap(const QVariantMap& in, int maxCount);

} // namespace PlasmaZones
