// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace PhosphorShellPicker {

/// One wallpaper the picker can offer.
struct Candidate
{
    /// Absolute path of the image file.
    QString path;
    /// Display name: the file's base name, or a Plasma package's name.
    QString name;
    /// Where it came from: "current", "file" or "package".
    QString source;

    bool operator==(const Candidate& other) const = default;
};

/**
 * @brief The wallpaper candidates the picker strip scrolls through.
 *
 * Scans the user's wallpaper directories (`~/.local/share/wallpapers` and
 * every `wallpapers` directory under `$XDG_DATA_DIRS`, so the Plasma
 * packages in `/usr/share/wallpapers` are included) for two shapes:
 *
 *  - plain image files directly in a directory, and
 *  - Plasma wallpaper packages, a directory with `contents/images/`, for
 *    which the largest image by the `WxH` in its file name is the
 *    candidate and the package's `metadata.json` (or `metadata.desktop`)
 *    supplies the name.
 *
 * The current wallpaper, when set, leads the list; every other candidate
 * follows in directory order, then name order, with duplicates (the same
 * file reached through two paths) dropped. The scan is synchronous and
 * on the GUI thread: it lists directories, it does not decode images.
 * The strip's thumbnails decode asynchronously through Image.
 */
class WallpaperCandidates : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// Directories to scan, in order. Defaults to `defaultDirectories()`.
    Q_PROPERTY(QStringList directories READ directories WRITE setDirectories NOTIFY directoriesChanged)
    /// The wallpaper in use, listed first. Empty when unknown.
    Q_PROPERTY(QString currentPath READ currentPath WRITE setCurrentPath NOTIFY currentPathChanged)
    /// The scanned list as `{ path, name, source }` maps, for a ListView.
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY candidatesChanged)
    Q_PROPERTY(int count READ count NOTIFY candidatesChanged)

public:
    explicit WallpaperCandidates(QObject* parent = nullptr);
    ~WallpaperCandidates() override;

    [[nodiscard]] QStringList directories() const;
    void setDirectories(const QStringList& directories);

    [[nodiscard]] QString currentPath() const;
    void setCurrentPath(const QString& path);

    [[nodiscard]] QVariantList candidates() const;
    [[nodiscard]] int count() const;

    /// The user's wallpaper directory followed by the system ones, every
    /// existing `wallpapers` directory on the XDG data path.
    [[nodiscard]] static QStringList defaultDirectories();

    /// Scan one directory: its image files and its Plasma packages, sorted
    /// by name within each shape (files first). Pure, for the tests.
    [[nodiscard]] static QList<Candidate> scanDirectory(const QString& directory);

    /// The image a Plasma wallpaper package offers: the largest under
    /// `contents/images/` by the WxH in its name, or empty when the
    /// directory is not a package.
    [[nodiscard]] static QString packageImage(const QString& packageDir);

    /// A package's display name from its metadata, or the directory name.
    [[nodiscard]] static QString packageName(const QString& packageDir);

    /// Whether a file name carries a raster image suffix the picker offers.
    [[nodiscard]] static bool isImageFile(const QString& fileName);

    /// Re-run the scan against `directories` and `currentPath`. Emits
    /// candidatesChanged only when the list differs from the last scan.
    Q_INVOKABLE void rescan();

Q_SIGNALS:
    void directoriesChanged();
    void currentPathChanged();
    void candidatesChanged();

private:
    QStringList m_directories;
    QString m_currentPath;
    QList<Candidate> m_candidates;
};

} // namespace PhosphorShellPicker
