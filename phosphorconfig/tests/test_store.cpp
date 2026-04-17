// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/Schema.h>
#include <PhosphorConfig/Store.h>

#include <QColor>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <limits>
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
        JsonBackend backend(m_path);
        Store store(&backend, makeSchema());

        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 800);
        QCOMPARE(store.read<bool>(QStringLiteral("Window"), QStringLiteral("Maximized")), false);
        QCOMPARE(store.read<QString>(QStringLiteral("Window"), QStringLiteral("Title")), QStringLiteral("Default"));
        QCOMPARE(store.read<double>(QStringLiteral("Appearance"), QStringLiteral("FontSize")), 12.0);
        QCOMPARE(store.read<QColor>(QStringLiteral("Appearance"), QStringLiteral("Accent")), QColor(Qt::blue));
    }

    void writeEmitsChangedSignal()
    {
        JsonBackend backend(m_path);
        Store store(&backend, makeSchema());

        QSignalSpy spy(&store, &Store::changed);
        store.write(QStringLiteral("Window"), QStringLiteral("Width"), 1024);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("Window"));
        QCOMPARE(spy.first().at(1).toString(), QStringLiteral("Width"));

        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 1024);
    }

    void resetRestoresSchemaDefault()
    {
        JsonBackend backend(m_path);
        Store store(&backend, makeSchema());

        store.write(QStringLiteral("Window"), QStringLiteral("Width"), 1024);
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 1024);

        store.reset(QStringLiteral("Window"), QStringLiteral("Width"));
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 800);
    }

    void resetGroupResetsEveryDeclaredKey()
    {
        JsonBackend backend(m_path);
        Store store(&backend, makeSchema());

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
        JsonBackend backend(m_path);
        Store store(&backend, makeSchema());

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
        JsonBackend backend(m_path);
        Store store(&backend, makeSchema());

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

        JsonBackend backend(m_path);
        Store store(&backend, s);

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

        JsonBackend backend(m_path);
        Store store(&backend, s);

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

        JsonBackend backend(m_path);
        Store store(&backend, s);

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

        JsonBackend backend(m_path);
        Store store(&backend, s);

        {
            auto g = store.backend()->group(QStringLiteral("X"));
            g->writeInt(QStringLiteral("N"), 999);
        }

        QCOMPARE(store.readVariant(QStringLiteral("X"), QStringLiteral("N")).toInt(), 10);
    }

    void read_undeclaredKeyReturnsDefault()
    {
        JsonBackend backend(m_path);
        // Seed a stored value under an undeclared key, bypassing the store.
        {
            auto g = backend.group(QStringLiteral("SomeGroup"));
            g->writeInt(QStringLiteral("undeclared"), 999);
        }

        Store store(&backend, makeSchema());

        // Undeclared keys must return the value-initialized default per the
        // header docstring, not whatever the backend happens to hold.
        QCOMPARE(store.read<int>(QStringLiteral("SomeGroup"), QStringLiteral("undeclared")), 0);
        QCOMPARE(store.read<QString>(QStringLiteral("SomeGroup"), QStringLiteral("undeclared")), QString());
        QCOMPARE(store.read<bool>(QStringLiteral("SomeGroup"), QStringLiteral("undeclared")), false);
        QCOMPARE(store.read<double>(QStringLiteral("SomeGroup"), QStringLiteral("undeclared")), 0.0);
        // readVariant's contract matches.
        QVERIFY(!store.readVariant(QStringLiteral("SomeGroup"), QStringLiteral("undeclared")).isValid());
    }

    void stringStorage_preservesJsonLookingText()
    {
        // writeString is always verbatim in the current IBackend contract
        // (no content-dependent JSON-shape reinterpretation). Strings that
        // happen to parse as JSON must still round-trip as strings; the
        // structured-native path is reserved for QVariantList/QVariantMap
        // values, which the Store routes through writeJson.
        Schema s;
        s.version = 1;
        KeyDef def;
        def.key = QStringLiteral("Raw");
        def.defaultValue = QString();
        def.expectedType = QMetaType::QString;
        s.groups[QStringLiteral("G")] = {def};

        JsonBackend backend(m_path);
        Store store(&backend, s);

        const QString jsonLike = QStringLiteral("[hi]"); // starts with '[' but not valid JSON
        store.write(QStringLiteral("G"), QStringLiteral("Raw"), jsonLike);
        QCOMPARE(store.read<QString>(QStringLiteral("G"), QStringLiteral("Raw")), jsonLike);

        const QString actualJson = QStringLiteral("[1,2,3]");
        store.write(QStringLiteral("G"), QStringLiteral("Raw"), actualJson);
        // Valid JSON must ALSO round-trip as the literal string — the old
        // data-dependent reshape is gone.
        QCOMPARE(store.read<QString>(QStringLiteral("G"), QStringLiteral("Raw")), actualJson);
    }

    void variantList_roundTripsAsNativeJson()
    {
        Schema s;
        s.version = 1;
        KeyDef def;
        def.key = QStringLiteral("Items");
        def.defaultValue = QVariant(QVariantList{});
        def.expectedType = QMetaType::QVariantList;
        s.groups[QStringLiteral("G")] = {def};

        JsonBackend backend(m_path);
        Store store(&backend, s);

        QVariantList value;
        value.append(1);
        value.append(2);
        value.append(QStringLiteral("three"));
        store.write(QStringLiteral("G"), QStringLiteral("Items"), value);

        const QVariantList got = store.readVariant(QStringLiteral("G"), QStringLiteral("Items")).toList();
        QCOMPARE(got.size(), 3);
        QCOMPARE(got.at(0).toInt(), 1);
        QCOMPARE(got.at(2).toString(), QStringLiteral("three"));
    }

    void write_int64_fallsBackToStringWhenOutOfRange()
    {
        Schema s;
        s.version = 1;
        s.groups[QStringLiteral("G")] = {
            {QStringLiteral("Big"), 0, QMetaType::Int},
        };

        JsonBackend backend(m_path);
        Store store(&backend, s);

        const qlonglong big = static_cast<qlonglong>(std::numeric_limits<int>::max()) + 1;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("does not fit in int")));
        store.write(QStringLiteral("G"), QStringLiteral("Big"), QVariant::fromValue(big));

        // Stored as a string — raw readString picks up the canonical form.
        auto g = store.backend()->group(QStringLiteral("G"));
        QCOMPARE(g->readString(QStringLiteral("Big")), QString::number(big));
    }

    void write_canonicalisesNonCanonicalDiskValue()
    {
        // Validator that strips whitespace from a comma list. The disk holds
        // a non-canonical " a , b ", and the caller writes the canonical form
        // "a,b". Both validate to the same value, so the OLD short-circuit
        // (which compared validator(disk) vs validator(input)) silently kept
        // the non-canonical disk value. The new behaviour compares against
        // the raw disk value, so the rewrite happens and the file ends up
        // canonical.
        Schema s;
        s.version = 1;
        auto canonicalise = [](const QVariant& v) -> QVariant {
            QStringList parts = v.toString().split(QLatin1Char(','));
            for (auto& p : parts) {
                p = p.trimmed();
            }
            parts.removeAll(QString());
            return QVariant(parts.join(QLatin1Char(',')));
        };
        s.groups[QStringLiteral("L")] = {
            {QStringLiteral("Items"), QString(), QMetaType::QString, QString(), canonicalise},
        };

        JsonBackend backend(m_path);
        Store store(&backend, s);

        // Seed disk with non-canonical value, bypassing the Store.
        {
            auto g = backend.group(QStringLiteral("L"));
            g->writeString(QStringLiteral("Items"), QStringLiteral(" a , b "));
        }

        // Write the canonical form through the Store. The flush-loop pattern
        // mimics what Settings::save does: read+write the same key.
        store.write(QStringLiteral("L"), QStringLiteral("Items"), QStringLiteral("a,b"));

        // The on-disk value should now be canonical, not the seeded garbage.
        auto g = backend.group(QStringLiteral("L"));
        QCOMPARE(g->readString(QStringLiteral("Items")), QStringLiteral("a,b"));
    }

    void write_rejectsUndeclaredKey()
    {
        JsonBackend backend(m_path);
        Store store(&backend, makeSchema());

        QSignalSpy spy(&store, &Store::changed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("rejecting write to undeclared key")));
        store.write(QStringLiteral("UnknownGroup"), QStringLiteral("undeclared"), 42);

        // No emission, no leak into the backend.
        QCOMPARE(spy.count(), 0);
        auto g = backend.group(QStringLiteral("UnknownGroup"));
        QVERIFY(!g->hasKey(QStringLiteral("undeclared")));
    }

    void importFromJson_rejectsNonNumericVersion()
    {
        // A snapshot whose version key is a string (e.g. exported then
        // accidentally stringified) must be refused. Without the explicit
        // type check, QJsonValue::toInt(0) collapses to 0 and the mismatch
        // test silently lets a malformed snapshot overwrite declared keys.
        JsonBackend backend(m_path);
        Schema schema = makeSchema();
        schema.version = 2;
        Store store(&backend, schema);

        store.write(QStringLiteral("Window"), QStringLiteral("Width"), 1024);

        QJsonObject snapshot;
        snapshot[QStringLiteral("_version")] = QStringLiteral("2"); // string, not int
        QJsonObject window;
        window[QStringLiteral("Width")] = 2048;
        snapshot[QStringLiteral("Window")] = window;

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not a JSON number")));
        store.importFromJson(snapshot);

        // Import was refused — the prior value stays.
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 1024);
    }

    void importFromJson_acceptsMissingVersionKey()
    {
        // A snapshot without any version key is acceptable — callers doing
        // partial imports shouldn't be required to stamp one in. Only an
        // explicitly present but wrongly-typed key is a hard error.
        JsonBackend backend(m_path);
        Store store(&backend, makeSchema());

        QJsonObject snapshot;
        QJsonObject window;
        window[QStringLiteral("Width")] = 333;
        snapshot[QStringLiteral("Window")] = window;

        store.importFromJson(snapshot);
        QCOMPARE(store.read<int>(QStringLiteral("Window"), QStringLiteral("Width")), 333);
    }

    void concurrentGroup_secondGroupIsReadOnly()
    {
#ifndef QT_NO_DEBUG
        // JsonGroup's single-active-group check is a Q_ASSERT_X in debug
        // builds — deliberately fail fast so callers catch the bug locally.
        // The read-only fallback only matters in release (NDEBUG), where the
        // assert is stripped and the backend must still protect the live
        // group. Skip the test in debug builds.
        QSKIP("Debug build: Q_ASSERT_X fires before the release-mode fallback runs.");
#else
        JsonBackend backend(m_path);
        // Seed a value so reads can verify the read-only fallback.
        {
            auto seed = backend.group(QStringLiteral("G"));
            seed->writeInt(QStringLiteral("N"), 42);
        }

        auto first = backend.group(QStringLiteral("G"));
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("other group\\(s\\) still active")));
        auto second = backend.group(QStringLiteral("G"));

        // Reads on the disabled group still work (they observe the shared
        // root), but writes are refused without mutating storage.
        QCOMPARE(second->readInt(QStringLiteral("N"), 0), 42);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("dropping writeInt")));
        second->writeInt(QStringLiteral("N"), 99);

        // The write was silently dropped — first (the live group) still sees 42.
        QCOMPARE(first->readInt(QStringLiteral("N"), 0), 42);
#endif
    }

    void write_uintLargerThanIntMaxFallsBackToString()
    {
        // QMetaType::UInt values above INT_MAX used to be cast directly,
        // wrapping to a negative signed value. Route through the same
        // uint64 range-check as ULongLong so oversized values persist as
        // strings rather than silently corrupting.
        Schema s;
        s.version = 1;
        s.groups[QStringLiteral("G")] = {
            {QStringLiteral("Big"), 0u, QMetaType::UInt},
        };

        JsonBackend backend(m_path);
        Store store(&backend, s);

        const uint big = static_cast<uint>(std::numeric_limits<int>::max()) + 1u;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("does not fit in int")));
        store.write(QStringLiteral("G"), QStringLiteral("Big"), QVariant::fromValue(big));

        // Stored as a string — raw readString picks up the canonical form.
        auto g = store.backend()->group(QStringLiteral("G"));
        QCOMPARE(g->readString(QStringLiteral("Big")), QString::number(big));
    }

    void read_int64RoundTripsThroughStringFallback()
    {
        // Oversized int64 values are persisted as strings by writeVariantTo
        // (covered by write_int64_fallsBackToStringWhenOutOfRange). The read
        // path for LongLong-typed keys must parse the string back so the
        // Store API round-trips — otherwise the default-fallback silently
        // drops the stored value.
        Schema s;
        s.version = 1;
        s.groups[QStringLiteral("G")] = {
            {QStringLiteral("Big"), qlonglong(0), QMetaType::LongLong},
        };

        JsonBackend backend(m_path);
        Store store(&backend, s);

        const qlonglong big = static_cast<qlonglong>(std::numeric_limits<int>::max()) + 12345;
        store.write(QStringLiteral("G"), QStringLiteral("Big"), QVariant::fromValue(big));

        const QVariant got = store.readVariant(QStringLiteral("G"), QStringLiteral("Big"));
        QCOMPARE(got.toLongLong(), big);
    }

    void read_uint64RoundTripsThroughStringFallback()
    {
        Schema s;
        s.version = 1;
        s.groups[QStringLiteral("G")] = {
            {QStringLiteral("Big"), qulonglong(0), QMetaType::ULongLong},
        };

        JsonBackend backend(m_path);
        Store store(&backend, s);

        const qulonglong big = static_cast<qulonglong>(std::numeric_limits<int>::max()) + 99ULL;
        store.write(QStringLiteral("G"), QStringLiteral("Big"), QVariant::fromValue(big));

        const QVariant got = store.readVariant(QStringLiteral("G"), QStringLiteral("Big"));
        QCOMPARE(got.toULongLong(), big);
    }

    void sharedBackend_refusesConflictingVersionStamp()
    {
        // Two Stores share one backend. Both have a version stamp; the
        // second one mismatches the first. The second construction must
        // NOT clobber the first stamp — otherwise the on-disk version key
        // would end up inconsistent with the Schema the first Store built
        // its migration chain against.
        JsonBackend backend(m_path);

        Schema first;
        first.version = 1;
        first.versionKey = QStringLiteral("_version");
        first.groups[QStringLiteral("G")] = {{QStringLiteral("K"), 0, QMetaType::Int}};
        Store storeA(&backend, first);

        Schema second;
        second.version = 2;
        second.versionKey = QStringLiteral("_version");
        second.groups[QStringLiteral("H")] = {{QStringLiteral("K"), 0, QMetaType::Int}};

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing to overwrite")));
        Store storeB(&backend, second);

        const auto [key, version] = backend.versionStamp();
        QCOMPARE(key, QStringLiteral("_version"));
        QCOMPARE(version, 1); // first Store's stamp wins.
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

        JsonBackend backend(m_path);
        Store store(&backend, schema);

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
