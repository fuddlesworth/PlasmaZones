// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_zone_ids.cpp
 * @brief A zone id names one layout (F552, l18_duplicate_zone_ids).
 *
 * The resnap's zone-position map merges every layout's zones and the remembered
 * zones of every window name them by id, so two layouts holding the same zone
 * id put windows in the wrong zone after a layout switch. The registry's
 * directory load and its import give a layout that copied another layout's
 * zone ids new ones, and the load writes them back to the user's file. The same
 * slot's shared re-slot helper (sameNumberedZones, F963(b)) is pinned here too.
 */

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>
#include <QUuid>
#include <memory>

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
// Fixed so the directory scan meets the original before the copy: it reads the
// files in name order, and a layout file is named after its layout id.
const QString kOriginalId = QStringLiteral("{00000000-0000-4000-8000-00000000000a}");
const QString kCopyId = QStringLiteral("{ffffffff-0000-4000-8000-00000000000b}");
} // namespace

class TestLayoutZoneIds : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutDir = m_guard->dataPath() + QStringLiteral("/plasmazones/layouts");
        QVERIFY(QDir().mkpath(m_layoutDir));
    }

    void cleanup()
    {
        m_guard.reset();
    }

    // A user file that copied another layout's zone ids loads with ids of its
    // own, the file holds them afterwards, and a later load keeps them. The
    // original, met first, keeps its ids. FAILS without the reissue in
    // loadLayoutsFromDirectory (the copy shares the original's ids) and without
    // the write-back (the file still holds the old ones).
    void load_reissuesZoneIdsTakenByAnotherLayout()
    {
        const QStringList shared = {QUuid::createUuid().toString(), QUuid::createUuid().toString()};
        QVERIFY(writeLayoutFile(kOriginalId, QStringLiteral("Original"), shared));
        QVERIFY(writeLayoutFile(kCopyId, QStringLiteral("Copy"), shared));

        QObject owner;
        PhosphorZones::LayoutRegistry* registry = loadedRegistry(&owner);
        QCOMPARE(zoneIdsOf(registry, kOriginalId), shared);
        const QStringList reissued = zoneIdsOf(registry, kCopyId);
        QCOMPARE(reissued.size(), shared.size());
        for (const QString& id : reissued) {
            QVERIFY2(!shared.contains(id), "the copy's zones have ids of their own");
        }
        QCOMPARE(zoneIdsOnDisk(kCopyId), reissued);

        PhosphorZones::LayoutRegistry* again = loadedRegistry(&owner);
        QCOMPARE(zoneIdsOf(again, kCopyId), reissued);
        QCOMPARE(zoneIdsOf(again, kOriginalId), shared);
    }

    // A copy saved under a name of its own (not <layout id>.json) is written
    // back under its layout id and the copy file goes, so the next load does
    // not meet the old ids again and keeps the new ones. FAILS without the
    // removal of the copied file (it stays with the original's ids, and the
    // second load re-mints).
    void load_copyUnderAnotherNameIsReplacedByItsLayoutFile()
    {
        const QStringList shared = {QUuid::createUuid().toString(), QUuid::createUuid().toString()};
        QVERIFY(writeLayoutFile(kOriginalId, QStringLiteral("Original"), shared));
        const QString copyPath = m_layoutDir + QStringLiteral("/my-copy.json");
        QVERIFY(writeJson(copyPath, layoutJson(kCopyId, QStringLiteral("Copy"), shared)));

        QObject owner;
        const QStringList reissued = zoneIdsOf(loadedRegistry(&owner), kCopyId);
        QCOMPARE(reissued.size(), shared.size());
        QVERIFY(!QFile::exists(copyPath));
        QCOMPARE(zoneIdsOnDisk(kCopyId), reissued);
        QCOMPARE(zoneIdsOf(loadedRegistry(&owner), kCopyId), reissued);
    }

    // An imported file holding a loaded layout's zone ids under another layout
    // id is given new ones. FAILS without the reissue in importLayout.
    void import_reissuesTakenZoneIds()
    {
        const QStringList shared = {QUuid::createUuid().toString()};
        QVERIFY(writeLayoutFile(kOriginalId, QStringLiteral("Original"), shared));
        QObject owner;
        PhosphorZones::LayoutRegistry* registry = loadedRegistry(&owner);

        const QString picked = m_guard->dataPath() + QStringLiteral("/picked.json");
        QVERIFY(writeJson(picked, layoutJson(kCopyId, QStringLiteral("Picked"), shared)));
        PhosphorZones::Layout* imported = registry->importLayout(picked);
        QVERIFY(imported);
        QCOMPARE(imported->zones().size(), 1);
        QVERIFY(imported->zones().first()->id().toString() != shared.first());
        QCOMPARE(zoneIdsOf(registry, kOriginalId), shared);
    }

    // A user copy overriding a system layout keeps that layout's id, and its
    // zone ids with it: a layout with the document's own id is not another
    // layout. FAILS if the reissue stops skipping the document's own id.
    void override_sameLayoutIdIsNotACollision()
    {
        QObject owner;
        const QStringList zones = {QUuid::createUuid().toString(), QUuid::createUuid().toString()};
        PhosphorZones::Layout* system =
            PhosphorZones::Layout::fromJson(layoutJson(kOriginalId, QStringLiteral("System"), zones), &owner);
        QVERIFY(system);
        QJsonObject userCopy = layoutJson(kOriginalId, QStringLiteral("User copy"), zones);
        QCOMPARE(PhosphorZones::LayoutUtils::reissueTakenZoneIds(userCopy, {system}), 0);
        QCOMPARE(zoneIdsIn(userCopy), zones);
    }

    // One document repeating a zone id keeps the first and re-mints the
    // repeat, the same way on every run. FAILS if the reissue checks only
    // other layouts.
    void reissue_duplicateWithinOneLayout()
    {
        const QString repeated = QUuid::createUuid().toString();
        QJsonObject first = layoutJson(kCopyId, QStringLiteral("Twice"), {repeated, repeated});
        QJsonObject second = first;
        QCOMPARE(PhosphorZones::LayoutUtils::reissueTakenZoneIds(first, {}), 1);
        QCOMPARE(PhosphorZones::LayoutUtils::reissueTakenZoneIds(second, {}), 1);
        const QStringList ids = zoneIdsIn(first);
        QCOMPARE(ids.first(), repeated);
        QVERIFY(ids.last() != repeated);
        QVERIFY(!QUuid::fromString(ids.last()).isNull());
        QCOMPARE(zoneIdsIn(second), ids);
    }

    // A derived id that another layout holds, or that the document keeps
    // before it, is derived again (F1052). FAILS if the reissue takes the
    // first derivation without checking it.
    void reissue_derivedIdCollisionIsDerivedAgain()
    {
        QObject owner;
        const QString shared = QUuid::createUuid().toString();
        const QUuid layoutId = QUuid::fromString(kCopyId);
        const QString derived0 = QUuid::createUuidV5(layoutId, shared + QStringLiteral("#0")).toString();
        const QString derived1 = QUuid::createUuidV5(layoutId, shared + QStringLiteral("#1")).toString();
        PhosphorZones::Layout* other = PhosphorZones::Layout::fromJson(
            layoutJson(kOriginalId, QStringLiteral("Other"), {shared, derived0}), &owner);
        QVERIFY(other);

        // Index 0: shared is taken, and so is its first derivation.
        QJsonObject takenCase = layoutJson(kCopyId, QStringLiteral("Taken"), {shared});
        QCOMPARE(PhosphorZones::LayoutUtils::reissueTakenZoneIds(takenCase, {other}), 1);
        const QString minted = zoneIdsIn(takenCase).first();
        QVERIFY(minted != shared);
        QVERIFY(minted != derived0);

        // Index 1: the first derivation is the id the document keeps at index 0.
        QJsonObject keptCase = layoutJson(kCopyId, QStringLiteral("Kept"), {derived1, shared});
        QCOMPARE(PhosphorZones::LayoutUtils::reissueTakenZoneIds(keptCase, {other}), 1);
        const QStringList ids = zoneIdsIn(keptCase);
        QCOMPARE(ids.first(), derived1);
        QVERIFY(ids.last() != derived1);
        QVERIFY(ids.last() != shared);
    }

    // A hand-written layout may leave a zone's id out (the schema requires only
    // its geometry). The reissue gives it one derived from the layout and its
    // position, so every load in every process names it alike: the daemon and
    // the settings app then agree on it, and a Zone rule picked in one matches
    // in the other. FAILS if a missing id is left to Zone::fromJson, which
    // mints a random one per parse.
    void reissue_missingIdIsDerivedTheSameOnEveryLoad()
    {
        const QString kept = QUuid::createUuid().toString();
        QJsonObject first = layoutJson(kCopyId, QStringLiteral("Hand written"), {kept, QString()});
        QJsonArray zones = first.value(PhosphorZones::ZoneJsonKeys::Zones).toArray();
        QJsonObject idless = zones.at(1).toObject();
        idless.remove(PhosphorZones::ZoneJsonKeys::Id);
        zones[1] = idless;
        first[PhosphorZones::ZoneJsonKeys::Zones] = zones;
        QJsonObject second = first;

        QCOMPARE(PhosphorZones::LayoutUtils::reissueTakenZoneIds(first, {}), 1);
        QCOMPARE(PhosphorZones::LayoutUtils::reissueTakenZoneIds(second, {}), 1);
        const QStringList ids = zoneIdsIn(first);
        QCOMPARE(ids.first(), kept);
        QVERIFY(!QUuid::fromString(ids.last()).isNull());
        QVERIFY(ids.last() != kept);
        QCOMPARE(zoneIdsIn(second), ids);

        // The parsed layout carries the derived id, not a fresh random one.
        QObject owner;
        PhosphorZones::Layout* layout = PhosphorZones::Layout::fromJson(first, &owner);
        QVERIFY(layout);
        QCOMPARE(layout->zones().size(), 2);
        QCOMPARE(layout->zones().at(1)->id().toString(), ids.last());
    }

    // Documents whose id is no UUID do not share their new zone ids: each
    // derives from its own random namespace, as Layout::fromJson gives each a
    // random layout id (F1052). FAILS if the null layout id is the namespace.
    void reissue_nullLayoutIdDerivesDistinctIds()
    {
        QObject owner;
        const QString shared = QUuid::createUuid().toString();
        PhosphorZones::Layout* other =
            PhosphorZones::Layout::fromJson(layoutJson(kOriginalId, QStringLiteral("Other"), {shared}), &owner);
        QVERIFY(other);
        QJsonObject first = layoutJson(QStringLiteral("not-a-uuid"), QStringLiteral("First"), {shared});
        QJsonObject second = layoutJson(QStringLiteral("not-a-uuid"), QStringLiteral("Second"), {shared});
        QCOMPARE(PhosphorZones::LayoutUtils::reissueTakenZoneIds(first, {other}), 1);
        QCOMPARE(PhosphorZones::LayoutUtils::reissueTakenZoneIds(second, {other}), 1);
        QVERIFY(zoneIdsIn(first).first() != shared);
        QVERIFY(zoneIdsIn(first).first() != zoneIdsIn(second).first());
    }

    // The re-slot the layout-switch populate and the desktop carry share: the
    // zones themselves when the target holds them, the zone at the first one's
    // position when it does not, nothing when the target is short of it.
    void sameNumberedZones_slotsByPosition()
    {
        QObject owner;
        PhosphorZones::Layout* from =
            PhosphorZones::Layout::fromJson(layoutJson(kOriginalId, QStringLiteral("From"),
                                                       {QUuid::createUuid().toString(), QUuid::createUuid().toString(),
                                                        QUuid::createUuid().toString()}),
                                            &owner);
        PhosphorZones::Layout* two = PhosphorZones::Layout::fromJson(
            layoutJson(kCopyId, QStringLiteral("Two"),
                       {QUuid::createUuid().toString(), QUuid::createUuid().toString()}),
            &owner);
        QVERIFY(from && two);
        const QHash<QString, int> positions = PhosphorZones::LayoutUtils::buildGlobalZonePositionMap({from, two});
        const QStringList fromZones = idsOf(from);
        const QStringList twoZones = idsOf(two);

        using PhosphorZones::LayoutUtils::sameNumberedZones;
        QCOMPARE(sameNumberedZones(fromZones.mid(0, 2), from, positions), fromZones.mid(0, 2));
        QCOMPARE(sameNumberedZones({fromZones.at(1), fromZones.at(2)}, two, positions), QStringList{twoZones.at(1)});
        QVERIFY(sameNumberedZones({fromZones.at(2)}, two, positions).isEmpty());
        QVERIFY(sameNumberedZones({fromZones.at(0)}, nullptr, positions).isEmpty());
    }

private:
    /// A layout document with one full-width strip per id in @p zoneIds.
    static QJsonObject layoutJson(const QString& layoutId, const QString& name, const QStringList& zoneIds)
    {
        PhosphorZones::Layout layout(name);
        for (int i = 0; i < zoneIds.size(); ++i) {
            auto* zone = new PhosphorZones::Zone();
            zone->setRelativeGeometry(QRectF(0, qreal(i) / zoneIds.size(), 1, 1.0 / zoneIds.size()));
            layout.addZone(zone);
        }
        QJsonObject json = layout.toJson();
        json[PhosphorZones::ZoneJsonKeys::Id] = layoutId;
        QJsonArray zones = json.value(PhosphorZones::ZoneJsonKeys::Zones).toArray();
        for (int i = 0; i < zones.size(); ++i) {
            QJsonObject zone = zones.at(i).toObject();
            zone[PhosphorZones::ZoneJsonKeys::Id] = zoneIds.at(i);
            zones[i] = zone;
        }
        json[PhosphorZones::ZoneJsonKeys::Zones] = zones;
        return json;
    }

    static QStringList zoneIdsIn(const QJsonObject& json)
    {
        QStringList ids;
        for (const QJsonValue& zone : json.value(PhosphorZones::ZoneJsonKeys::Zones).toArray()) {
            ids.append(QUuid::fromString(zone.toObject().value(PhosphorZones::ZoneJsonKeys::Id).toString()).toString());
        }
        return ids;
    }

    static QStringList idsOf(const PhosphorZones::Layout* layout)
    {
        QStringList ids;
        for (const PhosphorZones::Zone* zone : layout->zones()) {
            ids.append(zone->id().toString());
        }
        return ids;
    }

    /// False when the file cannot be written, so the caller's QVERIFY ends the row.
    static bool writeJson(const QString& path, const QJsonObject& json)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        const QByteArray bytes = QJsonDocument(json).toJson();
        return file.write(bytes) == bytes.size();
    }

    QString fileFor(const QString& layoutId) const
    {
        return m_layoutDir + QLatin1Char('/') + QUuid::fromString(layoutId).toString(QUuid::WithoutBraces)
            + QStringLiteral(".json");
    }

    bool writeLayoutFile(const QString& layoutId, const QString& name, const QStringList& zoneIds)
    {
        return writeJson(fileFor(layoutId), layoutJson(layoutId, name, zoneIds));
    }

    QStringList zoneIdsOnDisk(const QString& layoutId) const
    {
        QFile file(fileFor(layoutId));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return zoneIdsIn(QJsonDocument::fromJson(file.readAll()).object());
    }

    PhosphorZones::LayoutRegistry* loadedRegistry(QObject* owner) const
    {
        PhosphorZones::LayoutRegistry* registry =
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), owner);
        registry->setLayoutDirectory(m_layoutDir);
        registry->loadLayouts();
        return registry;
    }

    static QStringList zoneIdsOf(const PhosphorZones::LayoutRegistry* registry, const QString& layoutId)
    {
        const PhosphorZones::Layout* layout = registry->layoutById(QUuid::fromString(layoutId));
        return layout ? idsOf(layout) : QStringList();
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QString m_layoutDir;
};

QTEST_MAIN(TestLayoutZoneIds)
#include "test_layout_zone_ids.moc"
