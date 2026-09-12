// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace PhosphorRegistry {

/**
 * @brief A baseline payload plus keyed overrides, in insertion order.
 *
 * The container the shader-assignment trees each re-derived: a global baseline,
 * a map of per-key overrides, the order they were added in, and the handful of
 * mutators and accessors over both. `PhosphorAnimationShaders::ShaderProfileTree`,
 * `PhosphorSurfaceShaders::DecorationProfileTree` and
 * `PlasmaZones::OverlayShaderTree` all hold one of these now and forward to it.
 *
 * ## What this deliberately does NOT do
 *
 * It is not an attempt to make the four assignment trees one type. They
 * genuinely differ in key space (dot-paths against layout UUIDs), inheritance
 * model (a full walk-up against a single override-or-baseline step), payload
 * shape (per-field optionals against a plain struct) and parse dependency, and
 * collapsing those would force optionals onto a payload that deliberately has
 * none or cost the others their per-field inheritance. So `resolve()`, the seed
 * overlay, key normalisation and equality stay with each tree, where the
 * differences live.
 *
 * What was duplicated and should not have been is everything below: three
 * hand-written copies of the same storage, the same `setOverride` /
 * `clearOverride` / `overriddenPaths`, the same insertion-order bookkeeping, the
 * same empty-key guard, the same serialisation loop. They had already drifted —
 * one tree dropped its insertion-order list while the other two kept theirs, and
 * the empty-key guard was written out three times — which is the evidence that
 * lifting the CONSTANTS into a shared header while leaving the traversals
 * hand-written was never going to hold. The numbers were not the hard part.
 *
 * ## Insertion order is always tracked
 *
 * Even for a consumer that presents its keys sorted. It is one QStringList of
 * keys, and making it a policy knob would encode the very divergence this exists
 * to remove — a tree that wants sorted output sorts in its own accessor and is
 * free to ignore the order this keeps.
 *
 * @tparam Payload A value type. Needs copy-assignment and `operator==`; the
 *                 owning tree supplies its own `toJson` / `fromJson`, because the
 *                 root object's field names belong to the tree, not here.
 */
template<typename Payload>
class PathKeyedOverrides
{
public:
    // ─────── Baseline ───────

    const Payload& baseline() const
    {
        return m_baseline;
    }

    void setBaseline(const Payload& payload)
    {
        m_baseline = payload;
    }

    // ─────── Overrides ───────

    bool hasOverride(const QString& key) const
    {
        return m_overrides.contains(key);
    }

    /// The override stored at @p key, or a default-constructed payload when
    /// there is none. Returning the default rather than asserting is what lets a
    /// caller ask about a key it has not checked.
    Payload directOverride(const QString& key) const
    {
        return m_overrides.value(key);
    }

    /// Every overridden key, in the order it was first set.
    const QStringList& keys() const
    {
        return m_insertionOrder;
    }

    /// Store @p payload at @p key.
    ///
    /// An EMPTY key is refused. Every tree that used this guarded it separately,
    /// and for the same reason: the empty string is how each of them spells "the
    /// baseline", so accepting it as an override key would create an entry that
    /// shadows the baseline it is supposed to BE.
    void setOverride(const QString& key, const Payload& payload)
    {
        if (key.isEmpty()) {
            return;
        }
        if (!m_overrides.contains(key)) {
            m_insertionOrder.append(key);
        }
        m_overrides.insert(key, payload);
    }

    /// @return true when an override was actually removed.
    bool clearOverride(const QString& key)
    {
        if (m_overrides.remove(key) == 0) {
            return false;
        }
        m_insertionOrder.removeAll(key);
        return true;
    }

    void clearAllOverrides()
    {
        m_overrides.clear();
        m_insertionOrder.clear();
    }

    /// True when there is no override at all. Says nothing about the baseline,
    /// which a caller judges with its own payload-specific emptiness test.
    bool hasNoOverrides() const
    {
        return m_overrides.isEmpty();
    }

    // ─────── Ordered traversal ───────

    /// Invoke `fn(key, payload)` for every override, in insertion order.
    ///
    /// The shared half of serialisation, and deliberately only this half. Two of
    /// the three trees write their overrides as a JSON ARRAY of `{path, profile}`
    /// entries — which is why insertion order is part of their identity, it is
    /// observable on the wire — while the third writes a key-addressed object. A
    /// shared `overridesToJson` would have to take the field names as parameters
    /// and still only fit two of three: a knob pretending to be an abstraction.
    ///
    /// What they genuinely share is this walk and the guard inside it, which each
    /// had written out with the same comment attached. The entry shape stays with
    /// the tree that owns the format.
    template<typename Fn>
    void forEachInOrder(Fn fn) const
    {
        for (const QString& key : m_insertionOrder) {
            const auto it = m_overrides.constFind(key);
            if (it == m_overrides.constEnd()) {
                // Unreachable in practice: every mutator above writes the map and
                // the order list together, and they are the complete set. Kept as
                // a cheap guard rather than an assert, because a desync here would
                // only drop the orphaned entry rather than corrupt anything.
                continue;
            }
            fn(key, *it);
        }
    }

    // ─────── Equality pieces ───────
    //
    // Deliberately not an operator==. The trees disagree about whether insertion
    // ORDER is part of identity: the animation tree compares it, the overlay tree
    // is order-free by construction because every ordered view it exposes is
    // sorted. Handing each of them the piece it needs keeps that a stated
    // decision rather than something a shared operator quietly picks.

    bool sameBaseline(const PathKeyedOverrides& other) const
    {
        return m_baseline == other.m_baseline;
    }

    /// Compares the overrides as a SET: same keys, same payloads, order ignored.
    bool sameOverrides(const PathKeyedOverrides& other) const
    {
        if (m_overrides.size() != other.m_overrides.size()) {
            return false;
        }
        for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it) {
            const auto mine = other.m_overrides.constFind(it.key());
            if (mine == other.m_overrides.constEnd() || !(*mine == it.value())) {
                return false;
            }
        }
        return true;
    }

    bool sameKeyOrder(const PathKeyedOverrides& other) const
    {
        return m_insertionOrder == other.m_insertionOrder;
    }

private:
    Payload m_baseline;
    QHash<QString, Payload> m_overrides;
    QStringList m_insertionOrder;
};

} // namespace PhosphorRegistry
