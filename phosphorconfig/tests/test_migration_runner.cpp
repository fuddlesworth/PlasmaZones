// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/Schema.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace PhosphorConfig::tests {

class TestMigrationRunner : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        m_path = m_tmp->filePath(QStringLiteral("config.json"));
    }

    void cleanup()
    {
        m_tmp.reset();
    }

    void chain_runsEveryStepInOrder()
    {
        Schema schema;
        schema.version = 3;
        schema.migrations = {
            {1,
             [](QJsonObject& root) {
                 root[QStringLiteral("step1")] = true;
                 root[QStringLiteral("_version")] = 2;
             }},
            {2,
             [](QJsonObject& root) {
                 root[QStringLiteral("step2")] = true;
                 root[QStringLiteral("_version")] = 3;
             }},
        };

        QJsonObject root;
        root[QStringLiteral("_version")] = 1;

        MigrationRunner(schema).runInMemory(root);

        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 3);
        QVERIFY(root.value(QStringLiteral("step1")).toBool());
        QVERIFY(root.value(QStringLiteral("step2")).toBool());
    }

    void chain_abortsOnMissingVersionBump()
    {
        Schema schema;
        schema.version = 3;
        schema.migrations = {
            {1,
             [](QJsonObject& root) {
                 root[QStringLiteral("step1")] = true;
                 // Bug: forgot to bump _version.
             }},
            {2,
             [](QJsonObject& root) {
                 root[QStringLiteral("step2")] = true;
                 root[QStringLiteral("_version")] = 3;
             }},
        };

        QJsonObject root;
        root[QStringLiteral("_version")] = 1;

        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("did not bump '_version' to 2")));
        MigrationRunner(schema).runInMemory(root);

        // Chain aborted — step2 should NOT have run.
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 1);
        QVERIFY(!root.contains(QStringLiteral("step2")));
    }

    void runOnFile_rewritesAtomically()
    {
        // Seed a v1 file.
        QJsonObject original;
        original[QStringLiteral("_version")] = 1;
        original[QStringLiteral("stale")] = true;
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, original));

        Schema schema;
        schema.version = 2;
        schema.migrations = {
            {1,
             [](QJsonObject& root) {
                 root.remove(QStringLiteral("stale"));
                 root[QStringLiteral("fresh")] = 42;
                 root[QStringLiteral("_version")] = 2;
             }},
        };

        QVERIFY(MigrationRunner(schema).runOnFile(m_path));

        // File should now be v2.
        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject result = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(result.value(QStringLiteral("_version")).toInt(), 2);
        QVERIFY(!result.contains(QStringLiteral("stale")));
        QCOMPARE(result.value(QStringLiteral("fresh")).toInt(), 42);
    }

    void runOnFile_isIdempotent()
    {
        QJsonObject original;
        original[QStringLiteral("_version")] = 2;
        original[QStringLiteral("k")] = 1;
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, original));

        Schema schema;
        schema.version = 2;
        schema.migrations = {{1, [](QJsonObject&) { }}};

        // Already at current version — no-op, no rewrite.
        QVERIFY(MigrationRunner(schema).runOnFile(m_path));

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject result = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(result.value(QStringLiteral("_version")).toInt(), 2);
    }

    void runOnFile_freshInstallReturnsOk()
    {
        // Path doesn't exist — migration is a no-op, returns true.
        Schema schema;
        schema.version = 2;
        QVERIFY(MigrationRunner(schema).runOnFile(m_path));
        QVERIFY(!QFile::exists(m_path));
    }

    void customVersionKey()
    {
        Schema schema;
        schema.version = 2;
        schema.versionKey = QStringLiteral("schema_revision");
        schema.migrations = {
            {1,
             [](QJsonObject& root) {
                 root[QStringLiteral("schema_revision")] = 2;
             }},
        };

        QJsonObject root;
        root[QStringLiteral("schema_revision")] = 1;

        MigrationRunner(schema).runInMemory(root);
        QCOMPARE(root.value(QStringLiteral("schema_revision")).toInt(), 2);
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_path;
};

} // namespace PhosphorConfig::tests

QTEST_MAIN(PhosphorConfig::tests::TestMigrationRunner)
#include "test_migration_runner.moc"
