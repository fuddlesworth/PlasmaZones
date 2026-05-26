// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

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
    void resetToDefaults_clearsSourcePath();
};

void TestPaletteStore::defaults_loadsCanonicalPhosphorTokens()
{
    PaletteStore s;
    // Spot-check the canonical defaults — these are the load-bearing
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
    // The defaults differ from the just-loaded palette, so paletteChanged
    // fires once for the reset.
    QVERIFY(palSpy.count() >= 1);
    QCOMPARE(s.token(QStringLiteral("primary")), QColor("#3B82F6"));
}

QTEST_MAIN(TestPaletteStore)
#include "test_palettestore.moc"
