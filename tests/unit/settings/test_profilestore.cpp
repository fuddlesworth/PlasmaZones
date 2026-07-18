// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_profilestore.cpp
 * @brief Settings-profile persistence, inheritance resolution, and delta.
 *
 * ProfileStore is the store behind ProfilePageController::bridge. This test
 * drives it directly with stub closures (an in-memory "current config" and a
 * fixed "defaults" blob), so it exercises the delta/resolution engine without
 * a real PhosphorConfig::Store.
 *
 * Pinned behaviour:
 *   - A root profile stores only what differs from the defaults; activating it
 *     reproduces the captured config (diff → resolve round-trip).
 *   - A three-level chain stores each node's delta against its PARENT-resolved
 *     config, and resolving overlays the whole chain.
 *   - Deleting a middle node rebinds its children to its parent and re-flattens
 *     their deltas, leaving each child's resolved config unchanged.
 *   - A file stamped with a different schema version is refused (skipped on
 *     load, rejected on import).
 *   - The committed active pointer round-trips through index.json.
 */

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include "settings/profilestore.h"

using namespace PlasmaZones;

namespace {

// A tiny fixed schema-defaults blob (stamped like Store::exportToJson).
QJsonObject baseDefaults()
{
    return QJsonObject{
        {QStringLiteral("GroupA"), QJsonObject{{QStringLiteral("k1"), 1}, {QStringLiteral("k2"), QStringLiteral("x")}}},
        {QStringLiteral("GroupB"), QJsonObject{{QStringLiteral("b"), false}}},
        {QStringLiteral("_version"), 5},
    };
}

int groupAInt(const QJsonObject& blob, const QString& key)
{
    return blob.value(QStringLiteral("GroupA")).toObject().value(key).toInt(-999);
}

QString groupAStr(const QJsonObject& blob, const QString& key)
{
    return blob.value(QStringLiteral("GroupA")).toObject().value(key).toString();
}

} // namespace

class TestProfileStore : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir* m_dir = nullptr;
    ProfileStore* m_store = nullptr;
    QJsonObject m_current; // the stubbed "current live config"
    QJsonObject m_lastApplied; // captured from the applyConfig closure
    QString m_staged; // the stubbed staged active id

    QUuid idOf(const QString& braced) const
    {
        return QUuid(braced);
    }

    // Read a profile file's stored config delta straight off disk.
    QJsonObject storedConfig(const QString& bracedId) const
    {
        const QString path =
            m_dir->path() + QLatin1Char('/') + QUuid(bracedId).toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("config")).toObject();
    }

    QString storedParent(const QString& bracedId) const
    {
        const QString path =
            m_dir->path() + QLatin1Char('/') + QUuid(bracedId).toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("parent")).toString();
    }

private Q_SLOTS:
    void init()
    {
        m_dir = new QTemporaryDir();
        QVERIFY(m_dir->isValid());
        m_current = baseDefaults();
        m_lastApplied = QJsonObject();
        m_staged.clear();

        ProfileStore::Config config;
        config.profilesDir = [this]() {
            return m_dir->path();
        };
        config.currentConfig = [this]() {
            return m_current;
        };
        config.defaultConfig = []() {
            return baseDefaults();
        };
        config.applyConfig = [this](const QJsonObject& blob) {
            m_lastApplied = blob;
        };
        config.stagedActiveId = [this]() {
            return m_staged;
        };
        config.setStagedActiveId = [this](const QString& id) {
            m_staged = id;
        };
        config.formatVersion = 5;
        m_store = new ProfileStore(std::move(config));
    }

    void cleanup()
    {
        delete m_store;
        m_store = nullptr;
        delete m_dir;
        m_dir = nullptr;
    }

    /// A root profile stores only the delta vs defaults, and activating it
    /// reproduces the captured config.
    void rootDeltaRoundTrip()
    {
        // Current differs from defaults only in GroupA.k1 (1 → 2).
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};

        const QString id = m_store->createProfile(QStringLiteral("Root"), QString(), QString());
        QVERIFY(!id.isEmpty());

        // Only the changed key is stored.
        const QJsonObject delta = storedConfig(id);
        QCOMPARE(delta.value(QStringLiteral("GroupA")).toObject().value(QStringLiteral("k1")).toInt(), 2);
        QVERIFY(!delta.value(QStringLiteral("GroupA")).toObject().contains(QStringLiteral("k2")));
        QVERIFY(!delta.contains(QStringLiteral("GroupB")));

        // Move current away, then activate: the applied blob is the resolved profile.
        m_current = baseDefaults();
        QVERIFY(m_store->activateProfile(id));
        QCOMPARE(groupAInt(m_lastApplied, QStringLiteral("k1")), 2);
        QCOMPARE(groupAStr(m_lastApplied, QStringLiteral("k2")), QStringLiteral("x"));
        // The resolved blob carries the version marker so it round-trips through the store.
        QCOMPARE(m_lastApplied.value(QStringLiteral("_version")).toInt(), 5);
    }

    /// Three-level chain: each node stores its delta against its parent, and
    /// resolving overlays the whole chain.
    void threeLevelChain()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString root = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("y")}};
        const QString child = m_store->createProfile(QStringLiteral("C"), QString(), root);

        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 3}, {QStringLiteral("k2"), QStringLiteral("y")}};
        const QString grand = m_store->createProfile(QStringLiteral("G"), QString(), child);

        // C differs from R only in k2; G differs from C only in k1.
        const QJsonObject cDelta = storedConfig(child);
        QCOMPARE(cDelta.value(QStringLiteral("GroupA")).toObject().value(QStringLiteral("k2")).toString(),
                 QStringLiteral("y"));
        QVERIFY(!cDelta.value(QStringLiteral("GroupA")).toObject().contains(QStringLiteral("k1")));

        const QJsonObject gDelta = storedConfig(grand);
        QCOMPARE(gDelta.value(QStringLiteral("GroupA")).toObject().value(QStringLiteral("k1")).toInt(), 3);
        QVERIFY(!gDelta.value(QStringLiteral("GroupA")).toObject().contains(QStringLiteral("k2")));

        // Resolving G overlays defaults ← R ← C ← G.
        QVERIFY(m_store->activateProfile(grand));
        QCOMPARE(groupAInt(m_lastApplied, QStringLiteral("k1")), 3);
        QCOMPARE(groupAStr(m_lastApplied, QStringLiteral("k2")), QStringLiteral("y"));
    }

    /// Deleting a middle node rebinds its children to its parent and keeps their
    /// resolved config unchanged.
    void reparentOnDelete()
    {
        m_current = baseDefaults();
        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("x")}};
        const QString root = m_store->createProfile(QStringLiteral("R"), QString(), QString());

        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 2}, {QStringLiteral("k2"), QStringLiteral("y")}};
        const QString child = m_store->createProfile(QStringLiteral("C"), QString(), root);

        m_current[QStringLiteral("GroupA")] =
            QJsonObject{{QStringLiteral("k1"), 3}, {QStringLiteral("k2"), QStringLiteral("y")}};
        const QString grand = m_store->createProfile(QStringLiteral("G"), QString(), child);

        // Delete the middle node.
        QVERIFY(m_store->removeProfile(child));

        // G is rebound to R, and its resolved config is unchanged.
        QCOMPARE(storedParent(grand), root);
        QVERIFY(m_store->activateProfile(grand));
        QCOMPARE(groupAInt(m_lastApplied, QStringLiteral("k1")), 3);
        QCOMPARE(groupAStr(m_lastApplied, QStringLiteral("k2")), QStringLiteral("y"));

        // Only R and G remain.
        QCOMPARE(m_store->availableProfiles().size(), 2);
    }

    /// A file stamped with a different schema version is skipped on load and
    /// refused on import.
    void versionMismatchRefused()
    {
        const QUuid id = QUuid::createUuid();
        const QString path =
            m_dir->path() + QLatin1Char('/') + id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QJsonObject foreign{
            {QStringLiteral("_version"), 4},
            {QStringLiteral("id"), id.toString()},
            {QStringLiteral("name"), QStringLiteral("Old")},
            {QStringLiteral("parent"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("config"), QJsonObject{}},
        };
        QSaveFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(foreign).toJson());
        QVERIFY(f.commit());

        // Not surfaced by the list.
        QCOMPARE(m_store->availableProfiles().size(), 0);
        // Import of the same file is refused.
        QVERIFY(m_store->importProfile(path).isEmpty());
    }

    /// The committed active pointer round-trips through index.json.
    void activePointerRoundTrip()
    {
        m_current = baseDefaults();
        const QString id = m_store->createProfile(QStringLiteral("P"), QString(), QString());
        QVERIFY(m_store->committedActiveId().isEmpty());

        m_store->writeActiveId(id);
        QCOMPARE(m_store->committedActiveId(), id);

        m_store->writeActiveId(QString());
        QVERIFY(m_store->committedActiveId().isEmpty());
    }
};

QTEST_MAIN(TestProfileStore)
#include "test_profilestore.moc"
