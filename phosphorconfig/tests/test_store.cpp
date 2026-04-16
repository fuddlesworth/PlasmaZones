// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/Schema.h>
#include <PhosphorConfig/Store.h>

#include <QColor>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace PhosphorConfig::tests {

class TestStore : public QObject
{
    Q_OBJECT

private:
    Schema makeSchema()
    {
        Schema s;
        s.version = 1;
        s.groups[QStringLiteral("Window")] = {
            {QStringLiteral("Width"), 800, QMetaType::Int},
            {QStringLiteral("Height"), 600, QMetaType::Int},
            {QStringLiteral("Maximized"), false, QMetaType::Bool},
            {QStringLiteral("Title"), QStringLiteral("Default"), QMetaType::QString},
        };
        s.groups[QStringLiteral("Appearance")] = {
            {QStringLiteral("Accent"), QColor(Qt::blue), QMetaType::QColor},
            {QStringLiteral("FontSize"), 12.0, QMetaType::Double},
        };
        return s;
    }

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

    void readReturnsSchemaDefaultWhenKeyMissing()
    {
        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), makeSchema());

        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 800);
        QCOMPARE(store.read<bool>(QStringLiteral("Window"), QStringLiteral("Maximized")), false);
        QCOMPARE(store.read<QString>(QStringLiteral("Window"), QStringLiteral("Title")), QStringLiteral("Default"));
        QCOMPARE(store.read<double>(QStringLiteral("Appearance"), QStringLiteral("FontSize")), 12.0);
        QCOMPARE(store.read<QColor>(QStringLiteral("Appearance"), QStringLiteral("Accent")), QColor(Qt::blue));
    }

    void writeEmitsChangedSignal()
    {
        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), makeSchema());

        QSignalSpy spy(&store, &Store::changed);
        store.write(QStringLiteral("Window"), QStringLiteral("Width"), 1024);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("Window"));
        QCOMPARE(spy.first().at(1).toString(), QStringLiteral("Width"));

        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 1024);
    }

    void resetRestoresSchemaDefault()
    {
        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), makeSchema());

        store.write(QStringLiteral("Window"), QStringLiteral("Width"), 1024);
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 1024);

        store.reset(QStringLiteral("Window"), QStringLiteral("Width"));
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 800);
    }

    void resetGroupResetsEveryDeclaredKey()
    {
        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), makeSchema());

        store.write(QStringLiteral("Window"), QStringLiteral("Width"), 1024);
        store.write(QStringLiteral("Window"), QStringLiteral("Height"), 768);
        store.write(QStringLiteral("Window"), QStringLiteral("Maximized"), true);

        store.resetGroup(QStringLiteral("Window"));

        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 800);
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Height")), 600);
        QCOMPARE(store.read<bool>(QStringLiteral("Window"), QStringLiteral("Maximized")), false);
    }

    void exportToJsonIncludesEveryDeclaredKey()
    {
        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), makeSchema());

        store.write(QStringLiteral("Window"), QStringLiteral("Width"), 1024);

        const QJsonObject out = store.exportToJson();
        QVERIFY(out.contains(QStringLiteral("Window")));
        QVERIFY(out.contains(QStringLiteral("Appearance")));
        QCOMPARE(out.value(QStringLiteral("_version")).toInt(), 1);

        const QJsonObject window = out.value(QStringLiteral("Window")).toObject();
        QCOMPARE(window.value(QStringLiteral("Width")).toInt(), 1024);
        // Untouched keys come out as the schema default.
        QCOMPARE(window.value(QStringLiteral("Height")).toInt(), 600);
    }

    void importFromJsonOverwritesDeclaredKeysOnly()
    {
        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), makeSchema());

        QJsonObject snapshot;
        QJsonObject window;
        window[QStringLiteral("Width")] = 1920;
        window[QStringLiteral("Height")] = 1080;
        // An unknown key in the snapshot — should be ignored.
        window[QStringLiteral("UnknownKey")] = QStringLiteral("ignored");
        snapshot[QStringLiteral("Window")] = window;
        // An unknown group — should be ignored.
        snapshot[QStringLiteral("UnknownGroup")] = QJsonObject{{QStringLiteral("k"), 1}};

        store.importFromJson(snapshot);

        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 1920);
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Height")), 1080);
    }

    void validator_clampsOnRead()
    {
        Schema s;
        s.version = 1;
        s.groups[QStringLiteral("Window")] = {
            {QStringLiteral("Width"), 800, QMetaType::Int, QString(),
             [](const QVariant& v) {
                 return QVariant(qBound(100, v.toInt(), 2000));
             }},
        };

        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), s);

        // Write a too-large value directly via the backend, bypassing the
        // validator — simulates a hand-edited config or an older version.
        {
            auto g = store.backend()->group(QStringLiteral("Window"));
            g->writeInt(QStringLiteral("Width"), 99999);
        }

        // Read via the Store — validator should clamp to 2000.
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 2000);
    }

    void validator_clampsOnWrite()
    {
        Schema s;
        s.version = 1;
        s.groups[QStringLiteral("Window")] = {
            {QStringLiteral("Width"), 800, QMetaType::Int, QString(),
             [](const QVariant& v) {
                 return QVariant(qBound(100, v.toInt(), 2000));
             }},
        };

        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), s);

        store.write(QStringLiteral("Window"), QStringLiteral("Width"), 5);
        // Backend should have received the clamped value (100), not 5.
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 100);

        // And the raw backend read confirms the persisted value is clamped.
        {
            auto g = store.backend()->group(QStringLiteral("Window"));
            QCOMPARE(g->readInt(QStringLiteral("Width"), 0), 100);
        }
    }

    void validator_normalizesEnumString()
    {
        // Normalize an enum-style setting: accept { "fast", "medium", "slow" },
        // coerce anything else to "medium".
        Schema s;
        s.version = 1;
        s.groups[QStringLiteral("Perf")] = {
            {QStringLiteral("Mode"), QStringLiteral("medium"), QMetaType::QString, QString(),
             [](const QVariant& v) {
                 const QString str = v.toString();
                 if (str == QStringLiteral("fast") || str == QStringLiteral("medium")
                     || str == QStringLiteral("slow")) {
                     return QVariant(str);
                 }
                 return QVariant(QStringLiteral("medium"));
             }},
        };

        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), s);

        store.write(QStringLiteral("Perf"), QStringLiteral("Mode"), QStringLiteral("turbo"));
        QCOMPARE(store.read<QString>(QStringLiteral("Perf"), QStringLiteral("Mode")), QStringLiteral("medium"));

        store.write(QStringLiteral("Perf"), QStringLiteral("Mode"), QStringLiteral("slow"));
        QCOMPARE(store.read<QString>(QStringLiteral("Perf"), QStringLiteral("Mode")), QStringLiteral("slow"));
    }

    void validator_runsOnReadVariant()
    {
        Schema s;
        s.version = 1;
        s.groups[QStringLiteral("X")] = {
            {QStringLiteral("N"), 0, QMetaType::Int, QString(),
             [](const QVariant& v) {
                 return QVariant(qBound(0, v.toInt(), 10));
             }},
        };

        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), s);

        {
            auto g = store.backend()->group(QStringLiteral("X"));
            g->writeInt(QStringLiteral("N"), 999);
        }

        QCOMPARE(store.readVariant(QStringLiteral("X"), QStringLiteral("N")).toInt(), 10);
    }

    void migrationChainRunsOnConstruction()
    {
        // Seed an on-disk v1 document.
        QJsonObject original;
        original[QStringLiteral("_version")] = 1;
        original[QStringLiteral("Window")] = QJsonObject{{QStringLiteral("Width"), 640}};
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, original));

        Schema schema = makeSchema();
        schema.version = 2;
        schema.migrations = {
            {1,
             [](QJsonObject& root) {
                 // Rename "Width" → "NewWidth" under a new sub-group, bump version.
                 const QJsonObject window = root.value(QStringLiteral("Window")).toObject();
                 QJsonObject newWindow;
                 newWindow[QStringLiteral("Width")] = window.value(QStringLiteral("Width"));
                 root[QStringLiteral("Window")] = newWindow;
                 root[QStringLiteral("_version")] = 2;
             }},
        };
        // Schema needs to know about Width since our default re-read looks it up.
        schema.groups[QStringLiteral("Window")] = {
            {QStringLiteral("Width"), 0, QMetaType::Int},
        };

        auto backend = std::make_unique<JsonBackend>(m_path);
        Store store(std::move(backend), schema);

        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 640);

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject persisted = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(persisted.value(QStringLiteral("_version")).toInt(), 2);
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_path;
};

} // namespace PhosphorConfig::tests

QTEST_MAIN(PhosphorConfig::tests::TestStore)
#include "test_store.moc"
