// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace PhosphorAnimation {

class CurveRegistry;

/// Hierarchical profile storage with walk-up inheritance.
///
/// Sparse map from dot-paths (see ProfilePaths.h) to Profile overrides plus
/// a baseline. resolve() walks the chain and fills library defaults.
/// Schema-less beyond the dot-path convention — plugins add paths freely.
/// Not internally synchronized (value type).
class PHOSPHORANIMATION_EXPORT ProfileTree
{
public:
    ProfileTree() = default;

    ProfileTree(const ProfileTree&) = default;
    ProfileTree& operator=(const ProfileTree&) = default;
    ProfileTree(ProfileTree&&) = default;
    ProfileTree& operator=(ProfileTree&&) = default;

    /// Resolve effective Profile for @p path (walks parents, fills defaults).
    /// Every field is concrete on return — withDefaults() backfills any the
    /// chain left unset, including `curve` (a default Easing).
    Profile resolve(const QString& path) const;

    /// Overlay ONLY this tree's override chain for @p path onto @p base — the
    /// tree's own baseline is ignored and no library defaults are filled. Each
    /// engaged override field (closest-to-root first, leaf wins) replaces the
    /// matching field in @p base; any field that no override in the chain sets
    /// keeps its value from @p base. Returns @p base unchanged when no node in
    /// the chain has an override.
    ///
    /// Use this to layer per-event overrides on top of an externally-owned
    /// base profile (e.g. a consumer that holds the authoritative "global"
    /// elsewhere) without importing this tree's baseline — resolve() would
    /// instead start from m_baseline and collapse an empty chain to the
    /// library default.
    Profile overlayChainOnto(const QString& path, Profile base) const;

    /// Direct override at @p path without walking parents.
    /// Use hasOverride() to distinguish absent from all-unset.
    Profile directOverride(const QString& path) const;

    bool hasOverride(const QString& path) const;

    /// Every path with a direct override, in insertion order.
    QStringList overriddenPaths() const;

    /// Install an explicit override. Empty path is rejected (no-op).
    void setOverride(const QString& path, const Profile& profile);

    /// Remove the override at @p path. Returns true if one was removed.
    bool clearOverride(const QString& path);

    void clearAllOverrides();

    /// The baseline "global" profile — always participates in resolution.
    Profile baseline() const
    {
        return m_baseline;
    }
    void setBaseline(const Profile& profile);

    /// Serialize the entire tree. Overrides stored as JSON array (not object)
    /// so insertion order round-trips losslessly.
    QJsonObject toJson() const;

    /// Parse from JSON. Invalid entries fall back to default Profile; empty
    /// paths are dropped. @p registry forwarded to Profile::fromJson.
    static ProfileTree fromJson(const QJsonObject& obj, const CurveRegistry& registry);

    bool operator==(const ProfileTree& other) const;
    bool operator!=(const ProfileTree& other) const
    {
        return !(*this == other);
    }

private:
    static void overlay(Profile& dst, const Profile& src);

    /// Shared chain overlay for resolve() and overlayChainOnto(): walk @p path's
    /// ancestor chain (root → leaf) and overlay each engaged override onto
    /// @p seed, leaf winning. No baseline import, no default fill — the two
    /// public methods layer those on (resolve seeds m_baseline + withDefaults;
    /// overlayChainOnto seeds a caller base and returns raw).
    Profile overlayChain(const QString& path, Profile seed) const;

    Profile m_baseline;
    QHash<QString, Profile> m_overrides;
    QStringList m_insertionOrder;
};

} // namespace PhosphorAnimation
