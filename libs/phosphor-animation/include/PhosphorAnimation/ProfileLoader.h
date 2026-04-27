// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/CurveLoader.h> // LiveReload re-export
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <PhosphorJsonLoader/DirectoryLoader.h>
#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>

namespace PhosphorAnimation {

class CurveRegistry;
class PhosphorProfileRegistry;

/**
 * @brief Scans JSON profile-definition files and registers them with
 *        `PhosphorProfileRegistry`.
 *
 * Phase 4 decisions U + W + X + Y — symmetric with `CurveLoader`. The
 * loader is consumer-agnostic; callers supply the directory via their
 * own XDG namespace. User curves referenced by profiles must already be
 * registered (via `CurveLoader` running FIRST on the same namespace)
 * for the profile's `curve` field to resolve.
 *
 * Like `CurveLoader`, the directory-walking, watching, debouncing, and
 * user-wins-collision bookkeeping is delegated to
 * `PhosphorJsonLoader::DirectoryLoader`. This class is the profile-
 * specific sink on top of that.
 *
 * ## File format (schema v1)
 *
 * One profile per file, UTF-8 JSON object. Uses the existing
 * `Profile::toJson` / `Profile::fromJson` shape plus a top-level `name`
 * field that becomes the registry path:
 *
 * ```json
 * {
 *   "name": "overlay.fade",
 *   "curve": "spring:14.0,0.6",
 *   "duration": 250,
 *   "minDistance": 0
 * }
 * ```
 *
 * ## Commit semantics
 *
 * Each rescan produces a single `PhosphorProfileRegistry::reloadFromOwner`
 * call (decision W: coalesce). The registry emits per-path
 * `profileChanged(path)` for each path that actually changed in the
 * rescan; `profilesReloaded` is reserved for wholesale ops and is NOT
 * fired here. Bound consumers that subscribe per-path only wake for
 * paths that moved; avoiding N² re-resolution cost when several files
 * change at once falls out naturally because the registry's own diff
 * suppresses no-op updates.
 *
 * Profiles loaded here are preset templates — decision Y says settings
 * UIs deep-copy into the user's active profile rather than referencing
 * the preset live. This loader does not implement preset → active
 * linking.
 *
 * ## Thread safety
 *
 * Public methods (`entries()`, `hasPath()`, `loadFromDirectory`,
 * `requestRescan`, `registeredCount`, …) must be called from the thread
 * that owns this QObject (typically the GUI thread). `profilesChanged`
 * fires on that same thread. `PhosphorProfileRegistry` itself is
 * thread-safe via its own mutex; this loader's tracked-entries
 * bookkeeping is not.
 */
class PHOSPHORANIMATION_EXPORT ProfileLoader : public QObject
{
    Q_OBJECT

public:
    /// Construct a profile loader bound to @p registry for its entire
    /// lifetime. Entries committed by this loader are tagged with
    /// @p ownerTag in the registry's partitioned-ownership map — a
    /// later `reloadFromOwner(sameTag, ...)` replaces/removes ONLY
    /// this loader's entries, leaving daemon-fanned settings profiles
    /// and other loaders' entries untouched.
    ///
    /// If @p ownerTag is empty, a unique per-instance tag is generated
    /// ("profileloader-%1" with the object address) so two loaders
    /// built from the same consumer don't accidentally share a tag.
    /// Callers that want a stable human-readable tag (e.g. for tests
    /// or cross-process debugging) pass an explicit value.
    explicit ProfileLoader(PhosphorProfileRegistry& registry, CurveRegistry& curveRegistry,
                           const QString& ownerTag = {}, QObject* parent = nullptr);
    ~ProfileLoader() override;

    int loadFromDirectory(const QString& directory, LiveReload liveReload = LiveReload::Off);

    int loadFromDirectories(const QStringList& directories, LiveReload liveReload = LiveReload::Off);

    /// See `CurveLoader::loadLibraryBuiltins` — same semantics.
    int loadLibraryBuiltins(LiveReload liveReload = LiveReload::Off);

    /// Owner tag used for every registered entry. Exposed for tests
    /// and introspection; in production, derive nothing from this.
    QString ownerTag() const;

    void requestRescan();

    int registeredCount() const;

    struct Entry
    {
        QString path;
        QString sourcePath;
        QString systemSourcePath;
    };
    QList<Entry> entries() const;

    /// O(1) membership check. Prefer this over `entries()` when the
    /// caller only needs a contains test — `entries()` copies + sorts
    /// the full tracked set for deterministic iteration, which becomes
    /// expensive on hot paths (e.g. the daemon's
    /// `publishActiveAnimationProfile` runs per settings-slider tick).
    bool hasPath(const QString& path) const;

Q_SIGNALS:
    void profilesChanged();

private:
    class Sink;
    std::unique_ptr<Sink> m_sink;
    std::unique_ptr<PhosphorJsonLoader::DirectoryLoader> m_loader;
};

} // namespace PhosphorAnimation
