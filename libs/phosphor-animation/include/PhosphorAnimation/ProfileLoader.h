// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/CurveLoader.h> // LiveReload re-export
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/IDirectoryLoaderSink.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>

namespace PhosphorAnimation {

class CurveRegistry;
class PhosphorProfileRegistry;

/// Scans JSON profile-definition files and registers them with PhosphorProfileRegistry.
/// Shaped like CurveLoader, with two additions: a synchronous `rescanNow()`, for the
/// consumer that writes profile files and reads the registry back in the same call,
/// and an O(1) `hasPath()`. Nothing needs either of curves, so CurveLoader has no twins.
/// User curves must already be registered (CurveLoader first).
/// Profiles loaded here are preset templates — settings UIs deep-copy into active profiles.
class PHOSPHORANIMATION_EXPORT ProfileLoader : public QObject
{
    Q_OBJECT

public:
    /// If @p ownerTag is empty, a unique per-instance tag is generated.
    explicit ProfileLoader(PhosphorProfileRegistry& registry, CurveRegistry& curveRegistry,
                           const QString& ownerTag = {}, QObject* parent = nullptr);
    ~ProfileLoader() override;

    int loadFromDirectory(const QString& directory, LiveReload liveReload = LiveReload::Off);

    /// Scan multiple directories in caller-declared priority order.
    int loadFromDirectories(
        const QStringList& directories, LiveReload liveReload = LiveReload::Off,
        PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);

    int loadLibraryBuiltins(LiveReload liveReload = LiveReload::Off);

    QString ownerTag() const;
    void requestRescan();
    /// Rescan synchronously, so a caller that just wrote or deleted a
    /// profile file can read the registry back in the same call. The
    /// debounced `requestRescan` would answer that read with the
    /// pre-write state.
    ///
    /// GUI-thread only, like the rest of this class. `profilesChanged`
    /// (and the registry's own per-path `profileChanged` plus one
    /// `ownerReloaded`) are emitted on the CALLER's stack if the rescan
    /// sees a change, so a caller that is mid-mutation fans out into every
    /// directly-connected listener before this returns.
    ///
    /// Unlike `requestRescan`, this does NOT defer when a scan is already
    /// running, and nothing bounds the recursion: calling it from one of
    /// those handlers re-enters the scan, and a handler that calls it
    /// unconditionally recurses for as long as each scan keeps finding a
    /// change. The caller owns termination.
    void rescanNow();
    int registeredCount() const;

    struct Entry
    {
        QString path;
        QString sourcePath;
        QString systemSourcePath;
    };
    QList<Entry> entries() const;

    /// O(1) membership check — prefer over entries() on hot paths.
    bool hasPath(const QString& path) const;

Q_SIGNALS:
    void profilesChanged();

private:
    class Sink;
    std::unique_ptr<Sink> m_sink;
    std::unique_ptr<PhosphorFsLoader::DirectoryLoader> m_loader;
};

} // namespace PhosphorAnimation
