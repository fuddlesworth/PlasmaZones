// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/IDirectoryLoaderSink.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>

namespace PhosphorAnimation {

class CurveRegistry;

using LiveReload = PhosphorFsLoader::LiveReload;

/// Scans JSON curve-definition files and registers them with CurveRegistry.
/// Consumer-agnostic: callers supply directories via their own XDG namespace.
/// Directory walking / watching / collision resolution delegated to DirectoryLoader.
class PHOSPHORANIMATION_EXPORT CurveLoader : public QObject
{
    Q_OBJECT

public:
    explicit CurveLoader(CurveRegistry& registry, QObject* parent = nullptr);
    ~CurveLoader() override;

    /// Scan @p directory for *.json curve definitions and register each.
    int loadFromDirectory(const QString& directory, LiveReload liveReload = LiveReload::Off);

    /// Scan multiple directories in caller-declared priority order.
    int loadFromDirectories(
        const QStringList& directories, LiveReload liveReload = LiveReload::Off,
        PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);

    /// Load curves bundled at the library's install-prefix data directory.
    int loadLibraryBuiltins(LiveReload liveReload = LiveReload::Off);

    void requestRescan();
    int registeredCount() const;
    QString ownerTag() const;

    struct Entry
    {
        QString name;
        QString displayName;
        QString sourcePath;
        QString systemSourcePath; ///< Non-empty when this copy shadows a system entry.
    };
    QList<Entry> entries() const;

Q_SIGNALS:
    void curvesChanged();

private:
    class Sink;
    std::unique_ptr<Sink> m_sink;
    std::unique_ptr<PhosphorFsLoader::DirectoryLoader> m_loader;
};

} // namespace PhosphorAnimation
