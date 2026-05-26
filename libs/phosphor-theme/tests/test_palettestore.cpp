// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/PaletteStore.h>

#include <QColor>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using namespace PhosphorTheme;

class TestPaletteStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaults_loadsCanonicalPhosphorTokens();
    void loadFromJson_acceptsWrappedShape();
    void loadFromJson_acceptsFlatShape();
    void loadFromJson_mergesNotReplaces();
    void loadFromJson_rejectsInvalidPayload();
    void loadFromFile_persistsSourcePath();
    void loadFromFile_emitsErrorOnMissingFile();
    void applyTokens_emptyIsNoOp();
    void applyTokens_normalisesStringHexToColor();
    void resetToDefaults_clearsSourcePath();
    void hotReload_picksUpInPlaceEdit();
    void hotReload_picksUpAtomicRename();
};

void TestPaletteStore::defaults_loadsCanonicalPhosphorTokens()
{
    PaletteStore s;
    // Spot-check the canonical defaults, these are the load-bearing
    // values the rest of the shell will reference until matugen runs.
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
    QCOMPARE(s.token(QStringLiteral("background")), QColor("#050916"));
    QCOMPARE(s.token(QStringLiteral("brand_stop_3")), QColor("#F43F5E"));
    // Unknown token → invalid color.
    QVERIFY(!s.token(QStringLiteral("nonexistent")).isValid());
}

void TestPaletteStore::loadFromJson_acceptsWrappedShape()
{
    PaletteStore s;
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    const QByteArray payload = R"({"tokens": {"primary": "#112233", "background": "#445566"}})";
    QVERIFY(s.loadFromJson(payload));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));
    QCOMPARE(s.token(QStringLiteral("background")), QColor("#445566"));
    QCOMPARE(spy.count(), 1);
}

void TestPaletteStore::loadFromJson_acceptsFlatShape()
{
    PaletteStore s;
    const QByteArray payload = R"({"primary": "#aabbcc"})";
    QVERIFY(s.loadFromJson(payload));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#aabbcc"));
}

void TestPaletteStore::loadFromJson_mergesNotReplaces()
{
    // The merge contract: tokens absent from the new payload preserve
    // their prior value. brand_stop_* should survive a matugen run that
    // only emits M3 tokens.
    PaletteStore s;
    const QColor brandBefore = s.token(QStringLiteral("brand_stop_3"));
    QVERIFY(brandBefore.isValid());

    const QByteArray payload = R"({"tokens": {"primary": "#000000"}})";
    QVERIFY(s.loadFromJson(payload));
    QCOMPARE(s.token(QStringLiteral("brand_stop_3")), brandBefore);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#000000"));
}

void TestPaletteStore::loadFromJson_rejectsInvalidPayload()
{
    PaletteStore s;
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    // Outright malformed.
    QVERIFY(!s.loadFromJson("not json"));
    // Valid JSON with no usable color tokens.
    QVERIFY(!s.loadFromJson(R"({"tokens": {"primary": 42}})"));
    // Active palette is unchanged.
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
    QCOMPARE(spy.count(), 0);
}

void TestPaletteStore::loadFromFile_persistsSourcePath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"({"tokens": {"primary": "#cafe00"}})");
    f.close();

    PaletteStore s;
    QSignalSpy pathSpy(&s, &PaletteStore::sourcePathChanged);
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.sourcePath(), path);
    QCOMPARE(pathSpy.count(), 1);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#cafe00"));
}

void TestPaletteStore::loadFromFile_emitsErrorOnMissingFile()
{
    PaletteStore s;
    QSignalSpy errSpy(&s, &PaletteStore::loadError);
    QVERIFY(!s.loadFromFile(QStringLiteral("/definitely/does/not/exist.json")));
    QCOMPARE(errSpy.count(), 1);
}

void TestPaletteStore::applyTokens_emptyIsNoOp()
{
    PaletteStore s;
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    s.applyTokens({});
    QCOMPARE(spy.count(), 0);
}

void TestPaletteStore::applyTokens_normalisesStringHexToColor()
{
    // QML and matugen callers may hand us hex strings rather than QColor
    // values. applyPalette must normalise so token() returns a usable
    // QColor regardless of the original variant type.
    PaletteStore s;
    QVariantMap tokens;
    tokens.insert(QStringLiteral("primary"), QStringLiteral("#112233"));
    s.applyTokens(tokens);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));

    // Non-color, non-convertible values are dropped rather than
    // corrupting the palette.
    QVariantMap junk;
    junk.insert(QStringLiteral("on_primary"), QVariant::fromValue(42));
    QSignalSpy spy(&s, &PaletteStore::paletteChanged);
    s.applyTokens(junk);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(s.token(QStringLiteral("on_primary")), QColor("#F0F9FF")); // default unchanged
}

void TestPaletteStore::resetToDefaults_clearsSourcePath()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"({"primary": "#abcdef"})");
    f.close();

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));
    QVERIFY(!s.sourcePath().isEmpty());

    QSignalSpy pathSpy(&s, &PaletteStore::sourcePathChanged);
    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);
    s.resetToDefaults();
    QCOMPARE(s.sourcePath(), QString());
    QCOMPARE(pathSpy.count(), 1);
    QCOMPARE(palSpy.count(), 1);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
}

void TestPaletteStore::hotReload_picksUpInPlaceEdit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#112233"}})");
    }

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));

    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);

    // Edit in place (truncate + rewrite, the same syscall pattern most
    // editors use when configured for in-place save).
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"({"tokens": {"primary": "#445566"}})");
    }

    // QFileSystemWatcher is asynchronous: pump the event loop until it
    // fires or the deadline expires. 5s ceiling matches sibling tests.
    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#445566"));
}

void TestPaletteStore::hotReload_picksUpAtomicRename()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("p.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#112233"}})");
    }

    PaletteStore s;
    QVERIFY(s.loadFromFile(path));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#112233"));

    QSignalSpy palSpy(&s, &PaletteStore::paletteChanged);

    // Simulate vim's default save flow: write to a temp file, then
    // rename(2) over the destination. This unlinks the watched inode
    // and creates a new one, which drops QFileSystemWatcher's file
    // watch silently. The parent-directory watch armed in the ctor
    // is what catches the rename and re-arms the file watch.
    const QString tmpFile = path + QStringLiteral(".tmp");
    {
        QFile f(tmpFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"tokens": {"primary": "#445566"}})");
    }
    QVERIFY(QFile::remove(path));
    QVERIFY(QFile::rename(tmpFile, path));

    QVERIFY(palSpy.wait(5000));
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#445566"));
}

QTEST_MAIN(TestPaletteStore)
#include "test_palettestore.moc"
