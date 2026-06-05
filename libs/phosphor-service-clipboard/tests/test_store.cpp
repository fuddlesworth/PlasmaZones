// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit test for the on-disk clipboard store. It round-trips entries through a
// temporary directory (no real user data), pinning the persistence contract:
// save/load fidelity for text and binary content, sensitive-entry exclusion,
// orphan-blob pruning, and graceful empty/first-run behaviour.

#include "clipboardstore.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorServiceClipboard;

namespace {
ClipboardEntry textEntry(const QByteArray& content, const QString& preview)
{
    ClipboardEntry entry;
    entry.content = content;
    entry.mimeType = QStringLiteral("text/plain;charset=utf-8");
    entry.offeredTypes = {QStringLiteral("text/plain;charset=utf-8"), QStringLiteral("text/plain")};
    entry.preview = preview;
    entry.timestamp = QDateTime::fromMSecsSinceEpoch(1700000000000LL);
    entry.sensitive = false;
    return entry;
}
} // namespace

class ClipboardStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyOnFirstRun();
    void saveThenLoadRoundTrips();
    void binaryContentRoundTrips();
    void sensitiveEntryNotPersisted();
    void prunesOrphanBlobs();
    void emptySaveClearsHistory();
    void corruptIndexLoadsEmpty();
};

void ClipboardStoreTest::emptyOnFirstRun()
{
    QTemporaryDir dir;
    ClipboardStore store(dir.filePath(QStringLiteral("store")));
    QVERIFY(store.load().isEmpty());
}

void ClipboardStoreTest::saveThenLoadRoundTrips()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("store"));

    const QList<ClipboardEntry> entries = {textEntry("first", QStringLiteral("first")),
                                           textEntry("second", QStringLiteral("second"))};
    QVERIFY(ClipboardStore(path).save(entries));

    // A fresh store on the same directory reads them back, in order.
    const QList<ClipboardEntry> loaded = ClipboardStore(path).load();
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).content, QByteArray("first"));
    QCOMPARE(loaded.at(0).preview, QStringLiteral("first"));
    QCOMPARE(loaded.at(0).mimeType, QStringLiteral("text/plain;charset=utf-8"));
    QCOMPARE(loaded.at(0).offeredTypes.size(), 2);
    QCOMPARE(loaded.at(0).timestamp.toMSecsSinceEpoch(), 1700000000000LL);
    QCOMPARE(loaded.at(1).content, QByteArray("second"));
}

void ClipboardStoreTest::binaryContentRoundTrips()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("store"));

    ClipboardEntry image;
    image.content = QByteArray("\x89PNG\x00\x01\x02\xff", 8); // embedded NUL + high bytes
    image.mimeType = QStringLiteral("image/png");
    image.preview = QStringLiteral("[image/png, 8 bytes]");
    image.timestamp = QDateTime::fromMSecsSinceEpoch(1700000001000LL);

    QVERIFY(ClipboardStore(path).save({image}));
    const QList<ClipboardEntry> loaded = ClipboardStore(path).load();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).content, image.content);
    QCOMPARE(loaded.at(0).mimeType, QStringLiteral("image/png"));
}

void ClipboardStoreTest::sensitiveEntryNotPersisted()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("store"));

    ClipboardEntry secret = textEntry("hunter2", QStringLiteral("hunter2"));
    secret.sensitive = true;
    QVERIFY(ClipboardStore(path).save({textEntry("public", QStringLiteral("public")), secret}));

    const QList<ClipboardEntry> loaded = ClipboardStore(path).load();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).content, QByteArray("public"));
    // The secret's bytes are nowhere on disk.
    const QDir blobs(QDir(path).filePath(QStringLiteral("blobs")));
    for (const QString& name : blobs.entryList(QDir::Files)) {
        QFile blob(blobs.filePath(name));
        QVERIFY(blob.open(QIODevice::ReadOnly));
        QVERIFY(blob.readAll() != QByteArray("hunter2"));
    }
}

void ClipboardStoreTest::prunesOrphanBlobs()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("store"));
    const QDir blobs(QDir(path).filePath(QStringLiteral("blobs")));

    ClipboardStore store(path);
    QVERIFY(store.save({textEntry("a", QStringLiteral("a")), textEntry("b", QStringLiteral("b"))}));
    QCOMPARE(blobs.entryList(QDir::Files).size(), 2);

    // Re-saving without "b" must remove its now-orphaned blob.
    QVERIFY(store.save({textEntry("a", QStringLiteral("a"))}));
    QCOMPARE(blobs.entryList(QDir::Files).size(), 1);
    const QList<ClipboardEntry> loaded = store.load();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).content, QByteArray("a"));
}

void ClipboardStoreTest::emptySaveClearsHistory()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("store"));
    const QDir blobs(QDir(path).filePath(QStringLiteral("blobs")));

    ClipboardStore store(path);
    QVERIFY(store.save({textEntry("a", QStringLiteral("a"))}));
    QVERIFY(store.save({}));
    QVERIFY(store.load().isEmpty());
    QCOMPARE(blobs.entryList(QDir::Files).size(), 0);
}

void ClipboardStoreTest::corruptIndexLoadsEmpty()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("store"));
    QVERIFY(QDir().mkpath(path));
    QFile index(QDir(path).filePath(QStringLiteral("index.json")));
    QVERIFY(index.open(QIODevice::WriteOnly));
    index.write("{ this is not valid json");
    index.close();

    QVERIFY(ClipboardStore(path).load().isEmpty());
}

QTEST_GUILESS_MAIN(ClipboardStoreTest)
#include "test_store.moc"
