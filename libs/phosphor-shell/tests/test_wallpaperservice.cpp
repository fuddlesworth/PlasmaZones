// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// WallpaperService as the shell's own wallpaper store: the per-screen map
// and its all-screens entry, the transient preview layered over it, the
// persistence across instances, and the one-time seed from the previous
// desktop that happens only while the store is empty. Runs against a
// fixture XDG_CONFIG_HOME so nothing touches the developer's config.

#include <PhosphorShell/WallpaperService.h>

#include <PhosphorShaders/IWallpaperProvider.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

using PhosphorShell::WallpaperService;

// The seed source, with its calls counted through a pointer the test keeps
// (the service owns the provider and drops it after the seed).
class FakeProvider : public PhosphorShaders::IWallpaperProvider
{
public:
    FakeProvider(QString path, int* calls)
        : m_path(std::move(path))
        , m_calls(calls)
    {
    }

    QString wallpaperPath() override
    {
        ++*m_calls;
        return m_path;
    }

private:
    QString m_path;
    int* m_calls;
};

class TestWallpaperService : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void seedsFromTheDesktopOnlyWhileEmpty();
    void setPathPerScreenAndForAll();
    void previewLayersOverTheStoreUntilCleared();
    void rejectsWhatItCannotDraw();
    void pathIsDeterministicWithOnlyPerScreenEntries();
    void loadsTheImage();

private:
    std::unique_ptr<WallpaperService> make(const QString& seed);
    QJsonObject storedWallpapers() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_alpha;
    QString m_beta;
    QString m_notes;
    int m_seedCalls = 0;
};

void TestWallpaperService::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    // QStandardPaths reads the environment on every call, so pointing it
    // at the fixture here scopes the store to this test.
    qputenv("XDG_CONFIG_HOME", QDir(m_dir->path()).filePath(QStringLiteral("config")).toUtf8());
    m_seedCalls = 0;

    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    m_alpha = QDir(m_dir->path()).filePath(QStringLiteral("alpha.png"));
    m_beta = QDir(m_dir->path()).filePath(QStringLiteral("beta.png"));
    QVERIFY(image.save(m_alpha));
    image.fill(Qt::blue);
    QVERIFY(image.save(m_beta));
    m_notes = QDir(m_dir->path()).filePath(QStringLiteral("notes.txt"));
    QFile notes(m_notes);
    QVERIFY(notes.open(QIODevice::WriteOnly));
    notes.write("not an image");
    notes.close();
}

std::unique_ptr<WallpaperService> TestWallpaperService::make(const QString& seed)
{
    return std::make_unique<WallpaperService>(std::make_unique<FakeProvider>(seed, &m_seedCalls));
}

QJsonObject TestWallpaperService::storedWallpapers() const
{
    QFile file(WallpaperService::storePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("wallpapers")).toObject();
}

void TestWallpaperService::seedsFromTheDesktopOnlyWhileEmpty()
{
    QVERIFY(!QFile::exists(WallpaperService::storePath()));
    {
        auto service = make(m_alpha);
        QCOMPARE(m_seedCalls, 1);
        QCOMPARE(service->path(), m_alpha);
        QCOMPARE(service->configuredPath(QStringLiteral("DP-1")), m_alpha);
    }
    // The seed is persisted, under the all-screens key, in the store
    // beside the shell file.
    QVERIFY(WallpaperService::storePath().endsWith(QStringLiteral("/phosphor-shell/wallpaper.json")));
    QCOMPARE(storedWallpapers().value(QString()).toString(), m_alpha);

    // A populated store is authoritative: the desktop is never asked again,
    // even when it would now answer differently.
    {
        auto service = make(m_beta);
        QCOMPARE(m_seedCalls, 1);
        QCOMPARE(service->path(), m_alpha);
    }

    // A seed that points at nothing drawable leaves the store empty rather
    // than persisting a path the surface cannot load.
    QVERIFY(QFile::remove(WallpaperService::storePath()));
    {
        auto service = make(m_notes);
        QVERIFY(service->path().isEmpty());
        QVERIFY(!QFile::exists(WallpaperService::storePath()));
    }
}

void TestWallpaperService::setPathPerScreenAndForAll()
{
    auto service = make(m_alpha);
    QSignalSpy effective(service.get(), &WallpaperService::effectivePathChanged);
    QSignalSpy path(service.get(), &WallpaperService::pathChanged);

    // One screen: its entry, the others keep the all-screens wallpaper,
    // and the shell-wide `path` (which the lock screen reads) does not
    // move.
    QVERIFY(service->setPath(m_beta, QStringLiteral("DP-1")));
    QCOMPARE(service->configuredPath(QStringLiteral("DP-1")), m_beta);
    QCOMPARE(service->configuredPath(QStringLiteral("DP-2")), m_alpha);
    QCOMPARE(service->effectivePath(QStringLiteral("DP-1")), m_beta);
    QCOMPARE(service->path(), m_alpha);
    QCOMPARE(effective.count(), 1);
    QCOMPARE(effective.last().first().toString(), QStringLiteral("DP-1"));
    QCOMPARE(path.count(), 0);

    // Same value again: nothing changes, nothing fires.
    QVERIFY(service->setPath(m_beta, QStringLiteral("DP-1")));
    QCOMPARE(effective.count(), 1);

    // Persisted, and readable by a second instance without a seed.
    QCOMPARE(storedWallpapers().value(QStringLiteral("DP-1")).toString(), m_beta);
    {
        auto again = make(QString());
        QCOMPARE(again->configuredPath(QStringLiteral("DP-1")), m_beta);
        QCOMPARE(again->configuredPath(QStringLiteral("DP-2")), m_alpha);
    }

    // Every screen: the per-screen entry goes too, or DP-1 would keep
    // showing beta after the user asked every screen for alpha.
    QVERIFY(service->setPath(m_alpha, QString()));
    QCOMPARE(service->configuredPath(QStringLiteral("DP-1")), m_alpha);
    QCOMPARE(effective.count(), 2);
    QCOMPARE(effective.last().first().toString(), QString());
    QCOMPARE(storedWallpapers().size(), 1);

    QVERIFY(service->setPath(m_beta, QString()));
    QCOMPARE(service->path(), m_beta);
    QCOMPARE(path.count(), 1);
}

void TestWallpaperService::previewLayersOverTheStoreUntilCleared()
{
    auto service = make(m_alpha);
    QSignalSpy effective(service.get(), &WallpaperService::effectivePathChanged);
    const QJsonObject before = storedWallpapers();

    QVERIFY(service->setPreview(m_beta, QStringLiteral("DP-1")));
    QCOMPARE(service->previewPath(QStringLiteral("DP-1")), m_beta);
    QCOMPARE(service->effectivePath(QStringLiteral("DP-1")), m_beta);
    QCOMPARE(service->effectivePath(QStringLiteral("DP-2")), m_alpha);
    QCOMPARE(service->configuredPath(QStringLiteral("DP-1")), m_alpha);
    QCOMPARE(effective.count(), 1);
    // Transient: the store on disk is untouched.
    QCOMPARE(storedWallpapers(), before);

    // Repeating the preview is silent.
    QVERIFY(service->setPreview(m_beta, QStringLiteral("DP-1")));
    QCOMPARE(effective.count(), 1);

    service->clearPreview(QStringLiteral("DP-1"));
    QCOMPARE(service->effectivePath(QStringLiteral("DP-1")), m_alpha);
    QCOMPARE(effective.count(), 2);
    // Clearing what is not there is silent.
    service->clearPreview(QStringLiteral("DP-1"));
    QCOMPARE(effective.count(), 2);

    // An all-screens preview covers every output, over a per-screen one.
    QVERIFY(service->setPreview(m_beta, QStringLiteral("DP-2")));
    QVERIFY(service->setPreview(m_beta, QString()));
    QCOMPARE(service->effectivePath(QStringLiteral("DP-1")), m_beta);
    QCOMPARE(service->effectivePath(QStringLiteral("DP-2")), m_beta);
    QCOMPARE(effective.last().first().toString(), QString());
    service->clearPreview(QString());
    QCOMPARE(service->effectivePath(QStringLiteral("DP-2")), m_alpha);
    QVERIFY(service->previewPath(QStringLiteral("DP-2")).isEmpty());
}

void TestWallpaperService::rejectsWhatItCannotDraw()
{
    auto service = make(m_alpha);
    QSignalSpy effective(service.get(), &WallpaperService::effectivePathChanged);

    QVERIFY(!service->setPath(m_notes, QStringLiteral("DP-1")));
    QVERIFY(!service->setPath(QDir(m_dir->path()).filePath(QStringLiteral("missing.png")), QString()));
    QVERIFY(!service->setPreview(m_notes, QStringLiteral("DP-1")));
    QCOMPARE(service->effectivePath(QStringLiteral("DP-1")), m_alpha);
    QCOMPARE(effective.count(), 0);
    QCOMPARE(storedWallpapers().value(QString()).toString(), m_alpha);
}

void TestWallpaperService::pathIsDeterministicWithOnlyPerScreenEntries()
{
    // No seed and no all-screens entry: `path` is the first per-screen
    // entry by name, not whichever the hash yields.
    auto service = make(QString());
    QVERIFY(service->path().isEmpty());
    QVERIFY(service->setPath(m_beta, QStringLiteral("DP-2")));
    QCOMPARE(service->path(), m_beta);
    QVERIFY(service->setPath(m_alpha, QStringLiteral("DP-1")));
    QCOMPARE(service->path(), m_alpha);
    QVERIFY(service->configuredPath(QStringLiteral("DP-3")).isEmpty());
}

void TestWallpaperService::loadsTheImage()
{
    auto service = make(m_alpha);
    QTRY_VERIFY(service->isAvailable());
    QCOMPARE(service->image().size(), QSize(4, 4));
    QCOMPARE(service->image().format(), QImage::Format_RGBA8888);

    QSignalSpy image(service.get(), &WallpaperService::imageChanged);
    QVERIFY(service->setPath(m_beta, QString()));
    QTRY_COMPARE(image.count(), 1);
    QCOMPARE(service->image().pixelColor(0, 0), QColor(Qt::blue));
}

QTEST_MAIN(TestWallpaperService)
#include "test_wallpaperservice.moc"
