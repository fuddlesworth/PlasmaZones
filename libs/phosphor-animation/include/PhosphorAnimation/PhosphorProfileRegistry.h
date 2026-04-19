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

    /// Register or replace the profile at @p path. Fires
    /// `profileChanged(path)` on commit so bound consumers re-resolve.
    void registerProfile(const QString& path, const Profile& profile);

    /// Remove @p path. Fires `profileChanged(path)` if the path
    /// existed — bound consumers re-resolve and see `std::nullopt`,
    /// typically falling back to a default or holding the last
    /// resolved profile (consumer-defined).
    void unregisterProfile(const QString& path);

    /// Wholesale replace the entire registry contents with @p profiles
    /// and fire `profilesReloaded()` exactly once — provided the new
    /// map actually differs from the current one (no-op + no signal
    /// otherwise). Used by `ProfileLoader` after a directory rescan
    /// to coalesce many per-path signals into a single reload event.
    void reloadAll(const QHash<QString, Profile>& profiles);

    /// Clear the registry. Fires `profilesReloaded()`. Test-only
    /// semantics — production consumers don't typically want to wipe
    /// every registered profile at once.
    void clear();

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
};

} // namespace PhosphorAnimation
