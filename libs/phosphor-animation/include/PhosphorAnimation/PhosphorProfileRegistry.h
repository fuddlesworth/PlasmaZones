// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <mutex>
#include <optional>

namespace PhosphorAnimation {

/**
 * @brief Process-wide registry mapping profile path strings to `Profile` values.
 *
 * Phase 4 decision R — the path-string branch of `PhosphorMotionAnimation.profile`
 * resolves through this singleton. Consumers that want live-settings updates
 * (the success criterion: user-edited curve visibly affects shell animations
 * without a daemon restart) register their profiles here, then a settings
 * reload calls `registerProfile(path, updatedProfile)` to publish the update.
 * Every `PhosphorMotionAnimation` bound to `"<path>"` auto-re-resolves on the
 * emitted `profileChanged(path)` signal.
 *
 * ## Why a singleton (and not per-consumer DI like `CurveRegistry`)
 *
 * Process-global by design so that the QML plugin's
 * `PhosphorMotionAnimation` — a QML-instantiable type with no native
 * owner the consumer can inject into — has a stable anchor without
 * per-consumer dependency injection. Every QML element instantiated
 * through `import org.kde.phosphoranimation` must reach the same
 * registry instance the daemon populated, and QML has no ergonomic
 * hook for "pass this registry pointer through 15 layers of
 * declarative instantiation". The singleton is the simplest correct
 * shape for that.
 *
 * To prevent cross-instance state leakage in tests and on daemon
 * restart, consumers MUST call `clear()` (or the owner-scoped
 * `clearOwner(tag)`) on teardown and before re-populating at startup.
 * The `PlasmaZones::Daemon` composition root does both, mirroring its
 * equivalent lifecycle hooks for the daemon-owned `CurveRegistry`.
 *
 * ## Consumer model
 *
 * The registry is agnostic to path naming conventions. PlasmaZones populates
 * paths like `"shell.overlay.fade"` / `"snap.move"` etc.; a Wayfire plugin
 * or Quickshell shell uses whatever taxonomy suits its codebase. The
 * registry treats paths as opaque keys.
 *
 * ## Thread safety
 *
 * `resolve` / `registerProfile` / `unregisterProfile` / `clear` are
 * callable from any thread. An internal `std::mutex` guards the map.
 * `profileChanged` / `profilesReloaded` emit via Qt's signal machinery
 * which handles cross-thread delivery per connection type — consumers
 * that want GUI-thread delivery connect with `Qt::QueuedConnection` (or
 * default `AutoConnection` when the registry and receiver are on the
 * same thread).
 *
 * ## Lifetime
 *
 * Process-wide Meyers singleton. Destruction runs at process exit with
 * the usual no-dependency-on-other-singletons caveat; the registry
 * holds only `Profile` values (POD-ish) and doesn't reach into other
 * singletons, so ordering is irrelevant.
 */
class PHOSPHORANIMATION_EXPORT PhosphorProfileRegistry : public QObject
{
    Q_OBJECT

public:
    /// Process-wide singleton accessor.
    static PhosphorProfileRegistry& instance();

    /// Resolve @p path to a `Profile` if registered; `std::nullopt`
    /// otherwise. The returned value is a copy — mutations on it do
    /// not affect the registered profile.
    std::optional<Profile> resolve(const QString& path) const;

    /// Register or replace the profile at @p path with a direct
    /// (untagged) owner. Fires `profileChanged(path)` on commit so
    /// bound consumers re-resolve.
    ///
    /// Paths registered via this overload are NOT wiped by
    /// `reloadFromOwner` calls that use a different owner tag — they
    /// remain in place until explicitly unregistered. This is the
    /// fan-out path a shell daemon uses for settings-backed profiles.
    void registerProfile(const QString& path, const Profile& profile);

    /// Register or replace the profile at @p path, stamped with an
    /// owner tag. Used by loaders (or other partitioned publishers) so
    /// a later `reloadFromOwner(sameTag, ...)` can replace/remove only
    /// their own entries without disturbing entries owned by others.
    ///
    /// Last-writer-wins on the path itself — if an entry already
    /// exists with a different owner tag, it is overwritten. Callers
    /// that want user-claim-respecting semantics (e.g., a daemon
    /// whose settings-backed profiles yield to user JSON files) must
    /// check the existing entry's owner BEFORE re-registering.
    void registerProfile(const QString& path, const Profile& profile, const QString& ownerTag);

    /// Remove @p path. Fires `profileChanged(path)` if the path
    /// existed — bound consumers re-resolve and see `std::nullopt`,
    /// typically falling back to a default or holding the last
    /// resolved profile (consumer-defined).
    void unregisterProfile(const QString& path);

    /// Replace the subset of the registry owned by @p ownerTag with
    /// @p profiles. Entries owned by OTHER tags (including the
    /// empty/direct tag) are preserved untouched — this is the key
    /// property that prevents a loader rescan from evicting a daemon's
    /// settings-fanout profiles.
    ///
    /// Semantics per rescan:
    ///   - paths in the old set (owner == @p ownerTag) but not in the
    ///     new map are removed (user deleted their JSON file).
    ///   - paths in the new map are (re-)registered with @p ownerTag;
    ///     if the Profile value differs from the existing entry OR the
    ///     owner tag differs, a `profileChanged(path)` fires.
    ///   - if any change occurred, exactly one `profilesReloaded()`
    ///     fires at the end — consumers listening to the bulk signal
    ///     only see one event per rescan regardless of file count.
    ///
    /// An empty @p ownerTag is rejected (Q_ASSERT) — that would alias
    /// with the direct-owner path and defeat the partitioning.
    void reloadFromOwner(const QString& ownerTag, const QHash<QString, Profile>& profiles);

    /// Remove every entry owned by @p ownerTag. Fires
    /// `profilesReloaded()` if anything was removed.
    void clearOwner(const QString& ownerTag);

    /// Wholesale replace the entire registry contents with @p profiles
    /// regardless of owner tags. Every existing entry is discarded;
    /// every new entry is registered with the empty/direct owner.
    /// Fires `profilesReloaded()` exactly once — provided the new
    /// map actually differs from the current one.
    ///
    /// TEST-ONLY semantics: production consumers should use
    /// `reloadFromOwner` so a bulk replacement by one publisher does
    /// not evict entries owned by other publishers. Kept in the
    /// public API so existing test suites and introspection tools
    /// (that intentionally want a clean slate) still compile.
    void reloadAll(const QHash<QString, Profile>& profiles);

    /// Clear the registry. Fires `profilesReloaded()`. Test-only
    /// semantics — production consumers don't typically want to wipe
    /// every registered profile at once.
    void clear();

    /// Current owner tag for @p path, or empty string if the path is
    /// registered with the direct/default owner. Returns empty string
    /// if the path is not registered. Intended for tests and
    /// introspection.
    QString ownerOf(const QString& path) const;

    /// Current path count. Thread-safe — takes the lock to read.
    int profileCount() const;

    /// Is @p path registered? Thread-safe.
    bool hasProfile(const QString& path) const;

Q_SIGNALS:
    /// Fired when a profile is registered, replaced, or unregistered.
    /// Bound consumers — typically `PhosphorMotionAnimation` — re-resolve
    /// and rebind.
    void profileChanged(const QString& path);

    /// Fired when the registry is bulk-reloaded or cleared. Bound
    /// consumers re-resolve every path they hold rather than listening
    /// to N individual `profileChanged` signals.
    void profilesReloaded();

private:
    PhosphorProfileRegistry();
    ~PhosphorProfileRegistry() override;

    PhosphorProfileRegistry(const PhosphorProfileRegistry&) = delete;
    PhosphorProfileRegistry& operator=(const PhosphorProfileRegistry&) = delete;

    mutable std::mutex m_mutex;
    QHash<QString, Profile> m_profiles;
    /// Owner tag per registered path. Empty string = direct/default
    /// owner (the `registerProfile(path, profile)` overload). Non-empty
    /// = loader or other partitioned publisher; subject to replacement
    /// by `reloadFromOwner(tag, ...)` calls with the same tag.
    QHash<QString, QString> m_owners;
};

} // namespace PhosphorAnimation
