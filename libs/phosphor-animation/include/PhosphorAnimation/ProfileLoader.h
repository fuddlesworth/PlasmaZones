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
/// Shaped like CurveLoader, plus a synchronous `rescanNow()` for the consumer that
/// writes profile files and reads the registry back in the same call. Nothing needs
/// that of curves, so CurveLoader has no twin.
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
    /// GUI-thread only, like the rest of this class. Whatever the rescan
    /// emits, it emits on the CALLER's stack, so a caller that is
    /// mid-mutation fans out into every directly-connected listener before
    /// this returns.
    ///
    /// Two independent predicates decide what that is. `profilesChanged`
    /// fires when THIS loader's tracked set or a parsed Profile value
    /// changed. Separately, the registry emits a per-path `profileChanged`
    /// for each entry ITS diff moved, plus at most one `ownerReloaded` —
    /// and neither when that diff is empty, which includes a batch whose
    /// every path is already directly owned by someone else. The two can
    /// each fire without the other.
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

    /// O(1) membership check over this loader's OWN bookkeeping — prefer
    /// over entries() when that is the question. For "who owns this path in
    /// the registry", which is what a consumer usually wants, ask
    /// `PhosphorProfileRegistry::ownerOf()` instead: a path can be tracked
    /// here and yet be owned by a direct registration in the registry.
    bool hasPath(const QString& path) const;

Q_SIGNALS:
    void profilesChanged();

private:
    class Sink;
    std::unique_ptr<Sink> m_sink;
    std::unique_ptr<PhosphorFsLoader::DirectoryLoader> m_loader;
};

} // namespace PhosphorAnimation
