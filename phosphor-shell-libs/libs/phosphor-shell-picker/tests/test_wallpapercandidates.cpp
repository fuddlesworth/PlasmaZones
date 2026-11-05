// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// WallpaperCandidates over the fixture tree:
//
//   fixtures/wallpapers/
//     alpha.png            plain file
//     Beta.JPG             plain file, suffix case ignored
//     notes.txt            not an image
//     Empty/               a directory that is not a package
//     Nightbloom/          a Plasma package: two renditions, metadata.json
//       metadata.json
//       contents/images/1920x1080.png
//       contents/images/3840x2160.png

#include <PhosphorShellPicker/WallpaperCandidates.h>

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using PhosphorShellPicker::Candidate;
using PhosphorShellPicker::WallpaperCandidates;

namespace {

QString fixtures()
{
    return QDir(QStringLiteral(PHOSPHOR_PICKER_FIXTURES)).filePath(QStringLiteral("wallpapers"));
}

} // namespace

class TestWallpaperCandidates : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void imageSuffixes()
    {
        QVERIFY(WallpaperCandidates::isImageFile(QStringLiteral("a.png")));
        QVERIFY(WallpaperCandidates::isImageFile(QStringLiteral("a.JPG")));
        QVERIFY(WallpaperCandidates::isImageFile(QStringLiteral("a.webp")));
        QVERIFY(!WallpaperCandidates::isImageFile(QStringLiteral("a.txt")));
        QVERIFY(!WallpaperCandidates::isImageFile(QStringLiteral("a.svg")));
        QVERIFY(!WallpaperCandidates::isImageFile(QStringLiteral("noext")));
    }

    void scanFindsFilesThenPackages()
    {
        const QList<Candidate> found = WallpaperCandidates::scanDirectory(fixtures());
        QCOMPARE(found.size(), 3);
        QCOMPARE(found[0].name, QStringLiteral("alpha"));
        QCOMPARE(found[0].source, QStringLiteral("file"));
        QCOMPARE(found[1].name, QStringLiteral("Beta"));
        QCOMPARE(found[2].name, QStringLiteral("Nightbloom Package"));
        QCOMPARE(found[2].source, QStringLiteral("package"));
        QVERIFY(found[2].path.endsWith(QStringLiteral("/Nightbloom/contents/images/3840x2160.png")));
    }

    void packagePicksTheLargestRendition()
    {
        const QString image =
            WallpaperCandidates::packageImage(QDir(fixtures()).filePath(QStringLiteral("Nightbloom")));
        QVERIFY(image.endsWith(QStringLiteral("3840x2160.png")));
        QVERIFY(WallpaperCandidates::packageImage(QDir(fixtures()).filePath(QStringLiteral("Empty"))).isEmpty());
    }

    void packageNameFallsBackToTheDirectory()
    {
        QCOMPARE(WallpaperCandidates::packageName(QDir(fixtures()).filePath(QStringLiteral("Empty"))),
                 QStringLiteral("Empty"));
    }

    void scanOfMissingDirectoryIsEmpty()
    {
        QVERIFY(WallpaperCandidates::scanDirectory(QStringLiteral("/nonexistent/phosphor/wallpapers")).isEmpty());
    }

    void rescanLeadsWithTheCurrentWallpaperAndDedupes()
    {
        WallpaperCandidates candidates;
        candidates.setDirectories({fixtures()});
        // The current wallpaper is one of the scanned files, reached
        // through a different (non-canonical) path.
        candidates.setCurrentPath(fixtures() + QStringLiteral("/Empty/../Beta.JPG"));
        QSignalSpy changed(&candidates, &WallpaperCandidates::candidatesChanged);
        candidates.rescan();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(candidates.count(), 3);
        const QVariantList list = candidates.candidates();
        QCOMPARE(list[0].toMap().value(QStringLiteral("source")).toString(), QStringLiteral("current"));
        QCOMPARE(list[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Beta"));
        QCOMPARE(list[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("alpha"));
        QCOMPARE(list[2].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Nightbloom Package"));

        // An identical rescan is silent.
        candidates.rescan();
        QCOMPARE(changed.count(), 1);
    }

    void unknownCurrentPathIsSkipped()
    {
        WallpaperCandidates candidates;
        candidates.setDirectories({fixtures()});
        candidates.setCurrentPath(QStringLiteral("/nonexistent/wallpaper.png"));
        candidates.rescan();
        QCOMPARE(candidates.count(), 3);
        QCOMPARE(candidates.candidates()[0].toMap().value(QStringLiteral("source")).toString(), QStringLiteral("file"));
    }

    void directoriesAreScannedInOrder()
    {
        QTemporaryDir second;
        QVERIFY(second.isValid());
        QFile extra(second.filePath(QStringLiteral("zeta.png")));
        QVERIFY(extra.open(QIODevice::WriteOnly));
        extra.close();

        WallpaperCandidates candidates;
        candidates.setDirectories({second.path(), fixtures()});
        candidates.rescan();
        QCOMPARE(candidates.count(), 4);
        QCOMPARE(candidates.candidates()[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("zeta"));
    }
};

QTEST_GUILESS_MAIN(TestWallpaperCandidates)
#include "test_wallpapercandidates.moc"
