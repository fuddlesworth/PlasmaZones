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
 * Each rescan produces a single `PhosphorProfileRegistry::reloadAll`
 * call (decision W: coalesce). Bound consumers see one
 * `profilesReloaded` signal per scan instead of N per-path signals,
 * avoiding N² re-resolution cost when several files change at once.
 *
 * Profiles loaded here are preset templates — decision Y says settings
 * UIs deep-copy into the user's active profile rather than referencing
 * the preset live. This loader does not implement preset → active
 * linking.
 */
class PHOSPHORANIMATION_EXPORT ProfileLoader : public QObject
{
    Q_OBJECT

public:
    explicit ProfileLoader(QObject* parent = nullptr);
    ~ProfileLoader() override;

    int loadFromDirectory(const QString& directory, PhosphorProfileRegistry& registry,
                          LiveReload liveReload = LiveReload::Off);

    int loadFromDirectories(const QStringList& directories, PhosphorProfileRegistry& registry,
                            LiveReload liveReload = LiveReload::Off);

    int loadLibraryBuiltins(PhosphorProfileRegistry& registry);

    void requestRescan();

    int registeredCount() const;

    struct Entry
    {
        QString path;
        QString sourcePath;
        QString systemSourcePath;
    };
    QList<Entry> entries() const;

Q_SIGNALS:
    void profilesChanged();

private:
    class Sink;
    std::unique_ptr<Sink> m_sink;
    std::unique_ptr<PhosphorJsonLoader::DirectoryLoader> m_loader;
};

} // namespace PhosphorAnimation
