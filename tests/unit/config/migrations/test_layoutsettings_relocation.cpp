// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "config/configmigration.h"
#include "config/configdefaults.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorZones/LayoutSettingsStore.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutSettingsRelocation : public QObject
{
    Q_OBJECT

private:
    QJsonObject readJsonConfig(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    }

    void writeJsonRaw(const QString& path, const QJsonObject& obj)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(obj).toJson());
    }

    QString layoutsDirPath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/')
            + ConfigDefaults::layoutsSubdir();
    }

    QJsonObject zoneWithAppearance(const QString& zoneId, int zoneNumber) const
    {
        const QJsonObject relGeo{{QStringLiteral("x"), 0.0},
                                 {QStringLiteral("y"), 0.0},
                                 {QStringLiteral("width"), 1.0},
                                 {QStringLiteral("height"), 1.0}};
        const QJsonObject appearance{{QStringLiteral("useCustomColors"), true},
                                     {QStringLiteral("highlightColor"), QStringLiteral("#ff112233")}};
        return QJsonObject{{QStringLiteral("id"), zoneId},
                           {QStringLiteral("name"), QStringLiteral("Z%1").arg(zoneNumber)},
                           {QStringLiteral("zoneNumber"), zoneNumber},
                           {QStringLiteral("relativeGeometry"), relGeo},
                           {QStringLiteral("appearance"), appearance}};
    }

    QJsonObject fullLayoutWithSettings(const QString& layoutId) const
    {
        // Carries all 14 relocated per-layout setting keys so the relocation and
        // format-lock round-trip tests exercise the complete split, not a subset.
        return QJsonObject{
            {QStringLiteral("id"), layoutId},
            {QStringLiteral("name"), QStringLiteral("Settings Layout")},
            {QStringLiteral("showZoneNumbers"), false},
            {QStringLiteral("zonePadding"), 8},
            {QStringLiteral("outerGap"), 12},
            {QStringLiteral("usePerSideOuterGap"), true},
            {QStringLiteral("outerGapTop"), 1},
            {QStringLiteral("outerGapBottom"), 2},
            {QStringLiteral("outerGapLeft"), 3},
            {QStringLiteral("outerGapRight"), 4},
            {QStringLiteral("overlayDisplayMode"), 1},
            {QStringLiteral("autoAssign"), true},
            {QStringLiteral("hiddenFromSelector"), true},
            {QStringLiteral("useFullScreenGeometry"), true},
            {QStringLiteral("shaderId"), QStringLiteral("dissolve")},
            {QStringLiteral("shaderParams"), QJsonObject{{QStringLiteral("intensity"), 0.5}}},
            {QStringLiteral("zones"),
             QJsonArray{zoneWithAppearance(QStringLiteral("{11111111-0000-0000-0000-000000000001}"), 1)}},
        };
    }

    void writeLayoutFile(const QString& fileName, const QJsonObject& layout)
    {
        QDir().mkpath(layoutsDirPath());
        QFile f(layoutsDirPath() + QLatin1Char('/') + fileName);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(layout).toJson());
    }

    void writeLayoutFileWithSettings(const QString& fileName, const QString& layoutId)
    {
        writeLayoutFile(fileName, fullLayoutWithSettings(layoutId));
    }

    QByteArray readBytes(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private Q_SLOTS:

    void testLayoutSettingsRelocation_splitsLayoutFileIntoSidecar()
    {
        IsolatedConfigGuard guard;
        const QString layoutId = QStringLiteral("{abcd0000-0000-0000-0000-000000000000}");
        writeLayoutFileWithSettings(QStringLiteral("layout.json"), layoutId);

        QVERIFY(ConfigMigration::relocateLayoutSettings(layoutsDirPath(), ConfigDefaults::layoutSettingsFilePath()));

        // Layout file is slimmed: structural keys remain, settings gone.
        const QJsonObject slim =
            QJsonDocument::fromJson(readBytes(layoutsDirPath() + QStringLiteral("/layout.json"))).object();
        QVERIFY(slim.contains(QStringLiteral("id")));
        QVERIFY(slim.contains(QStringLiteral("zones")));
        QVERIFY(!slim.contains(QStringLiteral("zonePadding")));
        QVERIFY(!slim.contains(QStringLiteral("autoAssign")));
        QVERIFY(!slim.contains(QStringLiteral("hiddenFromSelector")));
        QVERIFY(!slim.contains(QStringLiteral("useFullScreenGeometry")));
        QVERIFY(!slim.contains(QStringLiteral("showZoneNumbers")));
        QVERIFY(!slim.value(QStringLiteral("zones")).toArray().at(0).toObject().contains(QStringLiteral("appearance")));

        // Sidecar carries the settings keyed by layout UUID, in store format.
        const QJsonObject sidecar = readJsonConfig(ConfigDefaults::layoutSettingsFilePath());
        QCOMPARE(sidecar.value(QStringLiteral("_version")).toInt(), 1);
        const QJsonObject settings = sidecar.value(layoutId).toObject();
        QCOMPARE(settings.value(QStringLiteral("zonePadding")).toInt(), 8);
        QCOMPARE(settings.value(QStringLiteral("autoAssign")).toBool(), true);
        QCOMPARE(settings.value(QStringLiteral("hiddenFromSelector")).toBool(), true);
        QCOMPARE(settings.value(QStringLiteral("useFullScreenGeometry")).toBool(), true);
        const QJsonObject zoneAppearance = settings.value(QStringLiteral("zoneAppearance")).toObject();
        QVERIFY(zoneAppearance.contains(QStringLiteral("{11111111-0000-0000-0000-000000000001}")));
    }

    void testLayoutSettingsRelocation_isIdempotent()
    {
        IsolatedConfigGuard guard;
        writeLayoutFileWithSettings(QStringLiteral("layout.json"),
                                    QStringLiteral("{abcd0000-0000-0000-0000-000000000000}"));

        QVERIFY(ConfigMigration::relocateLayoutSettings(layoutsDirPath(), ConfigDefaults::layoutSettingsFilePath()));
        const QByteArray layoutAfter1 = readBytes(layoutsDirPath() + QStringLiteral("/layout.json"));
        const QByteArray sidecarAfter1 = readBytes(ConfigDefaults::layoutSettingsFilePath());

        // Second pass over the already-slim file must not rewrite either file.
        QVERIFY(ConfigMigration::relocateLayoutSettings(layoutsDirPath(), ConfigDefaults::layoutSettingsFilePath()));
        QCOMPARE(readBytes(layoutsDirPath() + QStringLiteral("/layout.json")), layoutAfter1);
        QCOMPARE(readBytes(ConfigDefaults::layoutSettingsFilePath()), sidecarAfter1);
    }

    void testLayoutSettingsRelocation_missingDirIsNoOpSuccess()
    {
        IsolatedConfigGuard guard;
        QVERIFY(ConfigMigration::relocateLayoutSettings(layoutsDirPath(), ConfigDefaults::layoutSettingsFilePath()));
        QVERIFY(!QFile::exists(ConfigDefaults::layoutSettingsFilePath()));
    }

    // Format lock: the sidecar the migration writes MUST be consumable by
    // PhosphorZones::LayoutSettingsStore (the two split implementations are
    // duplicated by the migration-freeze policy). Load the migration's output
    // through the real store and reconstruct the original layout — proves the
    // formats agree end to end, not just by hand-asserted key shape.
    void testLayoutSettingsRelocation_outputRoundTripsThroughStore()
    {
        IsolatedConfigGuard guard;
        const QString layoutId = QStringLiteral("{abcd0000-0000-0000-0000-000000000000}");
        const QJsonObject original = fullLayoutWithSettings(layoutId);
        writeLayoutFile(QStringLiteral("layout.json"), original);

        QVERIFY(ConfigMigration::relocateLayoutSettings(layoutsDirPath(), ConfigDefaults::layoutSettingsFilePath()));

        PhosphorZones::LayoutSettingsStore store;
        QVERIFY(store.loadFromFile(ConfigDefaults::layoutSettingsFilePath()));
        const QJsonObject slim =
            QJsonDocument::fromJson(readBytes(layoutsDirPath() + QStringLiteral("/layout.json"))).object();

        const QJsonObject reconstructed =
            PhosphorZones::LayoutSettingsStore::mergeSettings(slim, store.settingsFor(layoutId));
        QCOMPARE(reconstructed, original);
    }

    void testLayoutSettingsRelocation_mergesIntoExistingSidecar()
    {
        IsolatedConfigGuard guard;
        // A sidecar already holding a different layout's entry (e.g. written by
        // the runtime store, or a prior partial run) must be preserved.
        const QString existingId = QStringLiteral("{eeee0000-0000-0000-0000-000000000000}");
        QDir().mkpath(QFileInfo(ConfigDefaults::layoutSettingsFilePath()).absolutePath());
        writeJsonRaw(ConfigDefaults::layoutSettingsFilePath(),
                     QJsonObject{{QStringLiteral("_version"), 1},
                                 {existingId, QJsonObject{{QStringLiteral("zonePadding"), 99}}}});

        const QString newId = QStringLiteral("{abcd0000-0000-0000-0000-000000000000}");
        writeLayoutFileWithSettings(QStringLiteral("layout.json"), newId);

        QVERIFY(ConfigMigration::relocateLayoutSettings(layoutsDirPath(), ConfigDefaults::layoutSettingsFilePath()));

        const QJsonObject sidecar = readJsonConfig(ConfigDefaults::layoutSettingsFilePath());
        QCOMPARE(sidecar.value(existingId).toObject().value(QStringLiteral("zonePadding")).toInt(), 99);
        QCOMPARE(sidecar.value(newId).toObject().value(QStringLiteral("zonePadding")).toInt(), 8);
    }

    void testLayoutSettingsRelocation_multipleLayoutsSelectiveZoneAppearance()
    {
        IsolatedConfigGuard guard;
        const QString idA = QStringLiteral("{aaaa0000-0000-0000-0000-000000000000}");
        const QString idB = QStringLiteral("{bbbb0000-0000-0000-0000-000000000000}");
        writeLayoutFileWithSettings(QStringLiteral("a.json"), idA);

        // Layout B: two zones, only the first has a custom appearance.
        const QJsonObject zone1 = zoneWithAppearance(QStringLiteral("{22222222-0000-0000-0000-000000000001}"), 1);
        QJsonObject zone2 = zoneWithAppearance(QStringLiteral("{22222222-0000-0000-0000-000000000002}"), 2);
        zone2.remove(QStringLiteral("appearance"));
        writeLayoutFile(QStringLiteral("b.json"),
                        QJsonObject{{QStringLiteral("id"), idB},
                                    {QStringLiteral("name"), QStringLiteral("B")},
                                    {QStringLiteral("zonePadding"), 3},
                                    {QStringLiteral("zones"), QJsonArray{zone1, zone2}}});

        QVERIFY(ConfigMigration::relocateLayoutSettings(layoutsDirPath(), ConfigDefaults::layoutSettingsFilePath()));

        const QJsonObject sidecar = readJsonConfig(ConfigDefaults::layoutSettingsFilePath());
        // Both layouts relocated; each keyed independently.
        QCOMPARE(sidecar.value(idA).toObject().value(QStringLiteral("zonePadding")).toInt(), 8);
        QCOMPARE(sidecar.value(idB).toObject().value(QStringLiteral("zonePadding")).toInt(), 3);
        // Only the zone that HAD an appearance is in B's zoneAppearance map.
        const QJsonObject bZoneAppearance =
            sidecar.value(idB).toObject().value(QStringLiteral("zoneAppearance")).toObject();
        QVERIFY(bZoneAppearance.contains(QStringLiteral("{22222222-0000-0000-0000-000000000001}")));
        QVERIFY(!bZoneAppearance.contains(QStringLiteral("{22222222-0000-0000-0000-000000000002}")));
    }
};

QTEST_MAIN(TestLayoutSettingsRelocation)
#include "test_layoutsettings_relocation.moc"
