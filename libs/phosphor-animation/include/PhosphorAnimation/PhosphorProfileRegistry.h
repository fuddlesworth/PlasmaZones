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

/**
 * @brief Registry mapping profile path strings to `Profile` values.
 *
 * Phase 4 decision R — the path-string branch of `PhosphorMotionAnimation.profile`
 * resolves through the registry the composition root publishes. Consumers that
 * want live-settings updates (the success criterion: user-edited curve visibly
 * affects shell animations without a daemon restart) register their profiles
 * here, then a settings reload calls `registerProfile(path, updatedProfile)`
 * to publish the update. Every `PhosphorMotionAnimation` bound to `"<path>"`
 * auto-re-resolves on the emitted `profileChanged(path)` signal.
 *
 * ## Ownership: composition-root DI for C++, service-locator for QML
 *
 * C++ consumers receive the registry by reference via constructor injection
 * (e.g. `SurfaceAnimator(PhosphorProfileRegistry&, ...)`) — that side is
 * straightforward dependency injection with no global state.
 *
 * The QML side cannot be wired the same way: `PhosphorMotionAnimation`
 * instances are created by the QML engine, not by a C++ owner with a
 * pointer to thread through. The `setDefaultRegistry` / `defaultRegistry`
 * static-handle pair is the bridge — a process-wide service locator that
 * the composition root publishes its locally-owned registry into so QML
 * callsites can find it. It is NOT a singleton in the lifetime sense
 * (each process owns exactly one registry, constructed and destructed by
 * the composition root, not lazily by the registry itself), but it IS a
 * service locator: any caller can read it, any caller can overwrite it,
 * and there is no compiler-enforced binding between writer and reader.
 * The composition root publishes once at startup and clears the handle
 * on teardown so a successive composition (tests, or a daemon
 * reconfigure cycle) does not dangle into freed memory. Tests that want
 * full isolation construct a per-fixture registry and bypass the static
 * altogether.
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
 * `setDefaultRegistry` / `defaultRegistry` are also thread-safe — the
 * static handle is `std::atomic<PhosphorProfileRegistry*>` with relaxed
 * ordering (the publishing composition root must have fully constructed
 * the registry before publishing, and the consumer side is a single
 * pointer dereference that needs no happens-before with any other memory).
 */
class PHOSPHORANIMATION_EXPORT PhosphorProfileRegistry : public QObject
{
    Q_OBJECT

public:
    explicit PhosphorProfileRegistry(QObject* parent = nullptr);
    ~PhosphorProfileRegistry() override;

    PhosphorProfileRegistry(const PhosphorProfileRegistry&) = delete;
    PhosphorProfileRegistry& operator=(const PhosphorProfileRegistry&) = delete;

    /// Publish @p registry as the process-wide default for QML callsites
    /// (`PhosphorMotionAnimation { profile: "<path>" }`) to resolve against.
    /// Called once by each composition root after constructing its own
    /// registry; pass `nullptr` on teardown to drop the handle before the
    /// registry destructs.
    static void setDefaultRegistry(PhosphorProfileRegistry* registry);

    /// Read-only view of the registry pointer published by
    /// `setDefaultRegistry`. Returns `nullptr` when no composition root
    /// has published yet — QML callsites then fall back to their library
    /// defaults rather than crashing.
    static PhosphorProfileRegistry* defaultRegistry();

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
    ///   - `profilesReloaded()` is NOT emitted — per-path signals cover
    ///     every change this method makes. Consumers that care about a
    ///     specific path listen to `profileChanged(path)`; consumers
    ///     that want a catch-all for wholesale registry mutation listen
    ///     to `profilesReloaded` (which fires only from `clear` /
    ///     `reloadAll`).
    ///   - `ownerReloaded(ownerTag)` fires exactly once AFTER the
    ///     per-path signal storm, provided the call made any changes.
    ///     Consumers that want to coalesce UI updates across a rescan
    ///     (settings list views, preset pickers rendering tens of
    ///     paths) latch on this instead of reacting to each per-path
    ///     `profileChanged`.
    ///
    /// An empty @p ownerTag is rejected (Q_ASSERT) — that would alias
    /// with the direct-owner path and defeat the partitioning.
    void reloadFromOwner(const QString& ownerTag, const QHash<QString, Profile>& profiles);

    /// Remove every entry owned by @p ownerTag. Fires one
    /// `profileChanged(path)` for each removed path (same shape as
    /// `reloadFromOwner`), followed by a single `ownerReloaded(ownerTag)`
    /// at the end of the batch if anything was removed. Does NOT fire
    /// `profilesReloaded()`.
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

    /// Fired only on wholesale operations (`reloadAll`, `clear`) where
    /// the registry cannot enumerate which individual paths changed.
    /// Bound consumers should re-resolve every path they hold on this
    /// signal.
    ///
    /// Targeted methods (`reloadFromOwner`, `clearOwner`,
    /// `registerProfile`, `unregisterProfile`) do NOT fire this signal
    /// — they emit per-path `profileChanged(path)` for every change so
    /// consumers only wake for paths that actually moved.
    void profilesReloaded();

    /// Fired at the end of a `reloadFromOwner` or `clearOwner` call
    /// whenever the call produced at least one per-path change. Exactly
    /// one emit per partitioned-reload batch, after every
    /// `profileChanged(path)` this call produced. Intended for UI views
    /// that want to coalesce per-path churn into one redraw rather than
    /// reacting to every individual `profileChanged`.
    ///
    /// Does NOT fire from `registerProfile`, `unregisterProfile`,
    /// `reloadAll`, or `clear` — those are not partitioned-reload
    /// operations.
    void ownerReloaded(const QString& ownerTag);

private:
    /// Atomic so concurrent QML loaders (multiple QQmlEngine instances
    /// on different threads — a background-prerender shell is the
    /// canonical case) cannot race on install-vs-read. Pointer loads
    /// are lock-free on every platform Qt supports; `relaxed` ordering
    /// is sufficient because the registry object's own initialisation
    /// is synchronised by the composition root's construction (the
    /// pointed-to `PhosphorProfileRegistry` is already fully constructed
    /// before the publishing thread calls setDefaultRegistry).
    static std::atomic<PhosphorProfileRegistry*> s_defaultRegistry;

    mutable std::mutex m_mutex;
    QHash<QString, Profile> m_profiles;
    /// Owner tag per registered path. Empty string = direct/default
    /// owner (the `registerProfile(path, profile)` overload). Non-empty
    /// = loader or other partitioned publisher; subject to replacement
    /// by `reloadFromOwner(tag, ...)` calls with the same tag.
    QHash<QString, QString> m_owners;
};

} // namespace PhosphorAnimation
