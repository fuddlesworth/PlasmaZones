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
/// Symmetric with CurveLoader. User curves must already be registered (CurveLoader first).
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
