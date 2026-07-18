// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <atomic>
#include <mutex>
#include <optional>

namespace PhosphorAnimation {

/// Registry mapping profile path strings to Profile values. Consumers
/// register profiles here; settings reloads call registerProfile() to
/// publish updates. Thread-safe (internal mutex).
class PHOSPHORANIMATION_EXPORT PhosphorProfileRegistry : public QObject
{
    Q_OBJECT

public:
    explicit PhosphorProfileRegistry(QObject* parent = nullptr);
    ~PhosphorProfileRegistry() override;

    PhosphorProfileRegistry(const PhosphorProfileRegistry&) = delete;
    PhosphorProfileRegistry& operator=(const PhosphorProfileRegistry&) = delete;

    /// Publish @p registry as the process-wide default for QML resolution.
    static void setDefaultRegistry(PhosphorProfileRegistry* registry);

    /// Read the process-wide default; nullptr if none published yet.
    static PhosphorProfileRegistry* defaultRegistry();

    /// Resolve @p path to a Profile if registered. Exact match only —
    /// returns `nullopt` if no entry exists at exactly @p path. For
    /// inheritance-aware resolution that walks the parent chain and
    /// overlays each level (the semantics every animation consumer
    /// actually wants — a parent-node override at `popup`
    /// SHOULD propagate to `popup.layoutPicker.show`), use
    /// @c resolveWithInheritance instead.
    std::optional<Profile> resolve(const QString& path) const;

    /// Resolve @p path with parent-chain inheritance. Walks
    /// `path → parent(path) → ... → "global"`, starting from a
    /// default-constructed `Profile`, and overlays each registered
    /// entry along the chain — every engaged optional field in a
    /// deeper entry replaces the shallower one (deeper-leaf-wins),
    /// while unset fields inherit from the parent. Falls through to
    /// the library defaults via `Profile::withDefaults()` so the
    /// returned value is always fully populated.
    ///
    /// **Two-layer overlay (when `lowPrecedenceOwnerTag` is set).**
    /// The walk runs in TWO passes: pass 1 overlays only entries
    /// owned by the low-precedence tag (seed-style defaults); pass 2
    /// overlays everything else (Settings publishes, user JSONs).
    /// Pass 2 always wins over pass 1, regardless of depth — so a
    /// user edit at the parent path `widget` (duration=800 ms) still
    /// cascades to a leaf like `widget.pulse.fast` even when the
    /// leaf has a seed entry (500 ms cubic-bezier). Without the
    /// two-layer model, leaf seeds would silently shadow parent
    /// user-overrides via the deeper-wins rule — exactly the bug
    /// the bundled-JSON deletion fixed.
    ///
    /// When @p lowPrecedenceOwnerTag is empty, the walk degrades to
    /// a single-pass deeper-wins overlay (the original semantics,
    /// kept for tests and consumers that don't use seed tagging).
    Profile resolveWithInheritance(const QString& path) const;
    Profile resolveWithInheritance(const QString& path, const QString& lowPrecedenceOwnerTag) const;

    /// Configure the owner tag whose entries should be treated as
    /// the lowest-precedence layer in `resolveWithInheritance`. Set
    /// once per registry from the composition root (daemon /
    /// settings / editor) right after the registry is constructed,
    /// using the conventional `kShellAnimationFamilySeedsOwnerTag`.
    /// Empty (default) preserves single-pass walk semantics.
    void setLowPrecedenceOwnerTag(const QString& tag);

    /// Register or replace the profile at @p path (direct/untagged owner).
    void registerProfile(const QString& path, const Profile& profile);

    /// Register or replace at @p path, stamped with an owner tag.
    void registerProfile(const QString& path, const Profile& profile, const QString& ownerTag);

    /// Remove @p path. Fires profileChanged if it existed.
    void unregisterProfile(const QString& path);

    /// Replace the subset owned by @p ownerTag with @p profiles.
    /// Entries owned by other tags are preserved.
    void reloadFromOwner(const QString& ownerTag, const QHash<QString, Profile>& profiles);

    /// Remove every entry owned by @p ownerTag.
    void clearOwner(const QString& ownerTag);

    /// Wholesale replace the entire registry. TEST-ONLY semantics.
    void reloadAll(const QHash<QString, Profile>& profiles);

    /// Clear the registry. Fires profilesReloaded().
    void clear();

    /// Current owner tag for @p path, or empty string.
    QString ownerOf(const QString& path) const;

    /// Thread-safe copy of every registered (path → Profile) pair,
    /// owner tags discarded. Intended for publishers that need to
    /// serialize the merged per-event profile set out of process
    /// (e.g. the daemon broadcasting a `ProfileTree` to the
    /// kwin-effect over D-Bus). For in-process resolution prefer
    /// `resolveWithInheritance` — it applies the parent-chain overlay
    /// the registry cannot encode in a flat snapshot.
    QHash<QString, Profile> snapshot() const;

    /// Like snapshot(), but omitting entries owned by the configured
    /// low-precedence tag (seed-style defaults). Publishers that
    /// serialize a ProfileTree for an out-of-process consumer MUST use
    /// this variant: tree overrides outrank the consumer's own baseline
    /// profile, so a flat snapshot would promote low-precedence seeds
    /// ABOVE the consumer's global profile — the exact shadowing
    /// setLowPrecedenceOwnerTag() exists to prevent (a `window` family
    /// seed would pin every window leg's duration and curve, turning
    /// the user's global animation settings into a no-op on the
    /// receiving side). With no low-precedence tag configured this is
    /// identical to snapshot().
    QHash<QString, Profile> snapshotExcludingLowPrecedence() const;

    /// Current path count. Thread-safe.
    int profileCount() const;

    /// Is @p path registered? Thread-safe.
    bool hasProfile(const QString& path) const;

Q_SIGNALS:
    /// Fired when a profile is registered, replaced, or unregistered.
    void profileChanged(const QString& path);

    /// Fired only on wholesale operations (reloadAll, clear).
    void profilesReloaded();

    /// Fired at the end of a reloadFromOwner/clearOwner batch if any changes occurred.
    void ownerReloaded(const QString& ownerTag);

private:
    static std::atomic<PhosphorProfileRegistry*> s_defaultRegistry;

    mutable std::mutex m_mutex;
    QHash<QString, Profile> m_profiles;
    QHash<QString, QString> m_owners;
    QString m_lowPrecedenceOwnerTag; ///< Layer-2 seed tag; see setLowPrecedenceOwnerTag.
};

} // namespace PhosphorAnimation
