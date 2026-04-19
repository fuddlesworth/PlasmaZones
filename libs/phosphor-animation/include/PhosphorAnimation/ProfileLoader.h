// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/CurveLoader.h> // LiveReload enum
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
QT_END_NAMESPACE

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
 * `name` is required and opaque to the loader — whatever
 * `PhosphorProfileRegistry` path the consumer wants to reference.
 * Conventional naming (`"overlay.fade"`, `"snap.move"`) is a consumer
 * decision.
 *
 * The `curve` field round-trips through `CurveRegistry::create` — any
 * curve registered by `CurveLoader` (including user-authored tunings)
 * resolves by name.
 *
 * ## Collision / live-reload / rescan
 *
 * Same shape as `CurveLoader` — see that class's doc for the full
 * contract. Later-scanned directories override earlier on path
 * collision; `QFileSystemWatcher` installed when `LiveReload::On`
 * with 50 ms debounce; `requestRescan()` entry point for consumer
 * D-Bus signals to tie into the same rescan.
 *
 * ## Apply semantics (decision Y)
 *
 * Profiles loaded here are **preset templates** living as read-only
 * files on disk. When a settings UI offers "Apply preset X" to the
 * user's active profile, it deep-copies the preset's fields into the
 * user's settings-backed profile (the Phase-4 migrated settings blob
 * — sub-commit 6). The on-disk preset is untouched; the user's
 * active profile is independent post-apply. Mirrors
 * `LayoutManager::duplicateLayout` semantics — this loader does NOT
 * provide a live-reference-back path.
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
    std::optional<std::pair<Entry, Profile>> parseFile(const QString& filePath) const;

    void rescanAll();
    void rescanDirectory(const QString& directory);
    void installWatcherIfNeeded();

    QPointer<PhosphorProfileRegistry> m_registry;
    QStringList m_directories;
    QHash<QString, Entry> m_entries; ///< path → entry
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer m_debounceTimer;
    bool m_liveReloadEnabled = false;
};

} // namespace PhosphorAnimation
