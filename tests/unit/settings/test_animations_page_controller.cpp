// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_page_controller.cpp
 * @brief Phase 0 round-trip + path-discovery tests for AnimationsPageController.
 *
 * Pins the file-per-path persistence model: setOverride writes one JSON
 * file under `<userProfilesDir>/<path>.json`, clearOverride deletes it,
 * resolvedProfile walks the parent chain and fills library defaults.
 *
 * Uses `setUserProfilesDirOverride()` to redirect file I/O into a
 * tmpdir so the test never touches the real user XDG dir.
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include "settings/animationspagecontroller.h"

using namespace PlasmaZones;

namespace {

QString readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

class TestAnimationsPageController : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Slider bounds ────────────────────────────────────────────────────

    void springBounds_matchPhosphorAnimationSpringDoc()
    {
        AnimationsPageController c;
        // Bounds are documented in PhosphorAnimation/Spring.h:
        //   omega ∈ [0.1, 200], zeta ∈ [0.0, 10.0]
        // The controller exposes them as CONSTANT properties so QML
        // sliders can bind once.
        QCOMPARE(c.springOmegaMin(), 0.1);
        QCOMPARE(c.springOmegaMax(), 200.0);
        QCOMPARE(c.springZetaMin(), 0.0);
        QCOMPARE(c.springZetaMax(), 10.0);
    }

    // ─── Path discovery ───────────────────────────────────────────────────

    void sectionForPath_extractsFirstSegment()
    {
        AnimationsPageController c;
        QCOMPARE(c.sectionForPath(QStringLiteral("global")), QStringLiteral("global"));
        QCOMPARE(c.sectionForPath(QStringLiteral("zone")), QStringLiteral("zone"));
        QCOMPARE(c.sectionForPath(QStringLiteral("zone.snapIn")), QStringLiteral("zone"));
        QCOMPARE(c.sectionForPath(QStringLiteral("panel.popup.layoutPicker.show")), QStringLiteral("panel"));
        QCOMPARE(c.sectionForPath(QString()), QString());
    }

    void eventLabel_titleCasesCamelLeaf()
    {
        AnimationsPageController c;
        QCOMPARE(c.eventLabel(QStringLiteral("global")), QStringLiteral("Global"));
        QCOMPARE(c.eventLabel(QStringLiteral("zone.snapIn")), QStringLiteral("Snap In"));
        QCOMPARE(c.eventLabel(QStringLiteral("panel.popup.layoutPicker")), QStringLiteral("Layout Picker"));
        QCOMPARE(c.eventLabel(QStringLiteral("panel.popup.layoutPicker.popIn")), QStringLiteral("Pop In"));
    }

    void parentChain_walksToGlobal()
    {
        AnimationsPageController c;
        const auto chain = c.parentChain(QStringLiteral("zone.snapIn"));
        QCOMPARE(chain, (QStringList{QStringLiteral("zone.snapIn"), QStringLiteral("zone"), QStringLiteral("global")}));
    }

    void parentChain_globalIsRoot()
    {
        AnimationsPageController c;
        const auto chain = c.parentChain(QStringLiteral("global"));
        QCOMPARE(chain, QStringList{QStringLiteral("global")});
    }

    void eventSections_groupsAllBuiltInPaths()
    {
        AnimationsPageController c;
        const QVariantList sections = c.eventSections();

        // Every built-in path must land in some section (no orphans).
        const QStringList paths = PhosphorAnimation::ProfilePaths::allBuiltInPaths();
        int totalListed = 0;
        QStringList allListed;
        for (const QVariant& sectionVar : sections) {
            const QVariantMap section = sectionVar.toMap();
            const QVariantList sPaths = section.value(QStringLiteral("paths")).toList();
            totalListed += sPaths.size();
            for (const QVariant& entry : sPaths) {
                allListed.append(entry.toMap().value(QStringLiteral("path")).toString());
            }
        }
        QCOMPARE(totalListed, paths.size());

        // No reserved path leaks through (allBuiltInPaths excludes them,
        // but pin the contract here).
        for (const QString& p : allListed) {
            QVERIFY2(!PhosphorAnimation::ProfilePaths::isReservedPath(p),
                     qPrintable(QStringLiteral("reserved path leaked into UI: ") + p));
        }
    }

    void eventSections_categoryFlagSetForParents()
    {
        AnimationsPageController c;
        const QVariantList sections = c.eventSections();

        // Find the "zone" section's "zone" entry — it should be flagged
        // isCategory=true because zone.snapIn etc. live under it.
        bool foundZoneCategory = false;
        bool zoneCategoryFlag = false;
        bool foundZoneSnapIn = false;
        bool zoneSnapInCategoryFlag = true; // pessimistic default
        for (const QVariant& sectionVar : sections) {
            const QVariantMap section = sectionVar.toMap();
            if (section.value(QStringLiteral("section")).toString() != QLatin1String("zone"))
                continue;
            for (const QVariant& entry : section.value(QStringLiteral("paths")).toList()) {
                const QVariantMap m = entry.toMap();
                const QString path = m.value(QStringLiteral("path")).toString();
                if (path == QLatin1String("zone")) {
                    foundZoneCategory = true;
                    zoneCategoryFlag = m.value(QStringLiteral("isCategory")).toBool();
                } else if (path == QLatin1String("zone.snapIn")) {
                    foundZoneSnapIn = true;
                    zoneSnapInCategoryFlag = m.value(QStringLiteral("isCategory")).toBool();
                }
            }
        }
        QVERIFY(foundZoneCategory);
        QVERIFY(foundZoneSnapIn);
        QVERIFY2(zoneCategoryFlag, "'zone' should be flagged as a category — it has children");
        QVERIFY2(!zoneSnapInCategoryFlag, "'zone.snapIn' is a leaf — not a category");
    }

    // ─── Override CRUD ────────────────────────────────────────────────────

    void setOverride_writesFileWithNameField()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);

        QVariantMap profile;
        profile.insert(QStringLiteral("duration"), 250);
        profile.insert(QStringLiteral("curve"), QStringLiteral("0.33,1,0.68,1"));
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), profile));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("zone.snapIn"));

        const QString filePath = tmp.path() + QStringLiteral("/zone.snapIn.json");
        QVERIFY(QFileInfo::exists(filePath));

        // Verify the on-disk shape: name field present, Profile fields preserved.
        const auto doc = QJsonDocument::fromJson(readFile(filePath).toUtf8());
        QVERIFY(doc.isObject());
        const QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QStringLiteral("name")).toString(), QStringLiteral("zone.snapIn"));
        QCOMPARE(obj.value(QStringLiteral("duration")).toInt(), 250);
        QCOMPARE(obj.value(QStringLiteral("curve")).toString(), QStringLiteral("0.33,1,0.68,1"));
    }

    void hasOverride_reflectsFileExistence()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(!c.hasOverride(QStringLiteral("zone.snapIn")));
        c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 200}});
        QVERIFY(c.hasOverride(QStringLiteral("zone.snapIn")));
    }

    void rawProfile_roundTripsProfileFieldsStripsName()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVariantMap input;
        input.insert(QStringLiteral("duration"), 175);
        input.insert(QStringLiteral("minDistance"), 8);
        // Caller-supplied `name` must be overwritten — rawProfile strips
        // `name` on read, so the only way this round-trips is if the
        // controller stamps the path on write.
        input.insert(QStringLiteral("name"), QStringLiteral("garbage"));
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), input));

        const QVariantMap raw = c.rawProfile(QStringLiteral("osd.show"));
        QVERIFY(!raw.contains(QStringLiteral("name")));
        QCOMPARE(raw.value(QStringLiteral("duration")).toInt(), 175);
        QCOMPARE(raw.value(QStringLiteral("minDistance")).toInt(), 8);
    }

    void clearOverride_removesFileAndEmits()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 200}});
        QVERIFY(c.hasOverride(QStringLiteral("osd.show")));

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(c.clearOverride(QStringLiteral("osd.show")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("osd.show"));
        QVERIFY(!c.hasOverride(QStringLiteral("osd.show")));
    }

    void clearOverride_noFileReturnsFalseNoSignal()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(!c.clearOverride(QStringLiteral("zone.snapIn")));
        QCOMPARE(spy.count(), 0);
    }

    void setOverride_emptyPathRejected()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(!c.setOverride(QString(), {{QStringLiteral("duration"), 100}}));
        QCOMPARE(spy.count(), 0);
    }

    // ─── Effective resolution ─────────────────────────────────────────────

    void resolvedProfile_unsetReturnsLibraryDefaults()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // No registry, no files. Walk falls through to library defaults.
        const QVariantMap resolved = c.resolvedProfile(QStringLiteral("zone.snapIn"));
        using P = PhosphorAnimation::Profile;
        QCOMPARE(resolved.value(QStringLiteral("duration")).toDouble(), P::DefaultDuration);
        QCOMPARE(resolved.value(QStringLiteral("minDistance")).toInt(), P::DefaultMinDistance);
        QCOMPARE(resolved.value(QStringLiteral("sequenceMode")).toInt(), int(P::DefaultSequenceMode));
        QCOMPARE(resolved.value(QStringLiteral("staggerInterval")).toInt(), P::DefaultStaggerInterval);
    }

    void resolvedProfile_walksParentChain()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Override the parent (zone) but not the leaf (zone.snapIn). The
        // leaf must inherit the parent's duration via walk-up.
        QVERIFY(c.setOverride(QStringLiteral("zone"), {{QStringLiteral("duration"), 222}}));

        const QVariantMap resolved = c.resolvedProfile(QStringLiteral("zone.snapIn"));
        QCOMPARE(resolved.value(QStringLiteral("duration")).toInt(), 222);
    }

    void resolvedProfile_leafOverridesParent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("zone"), {{QStringLiteral("duration"), 100}}));
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 333}}));

        const QVariantMap resolved = c.resolvedProfile(QStringLiteral("zone.snapIn"));
        QCOMPARE(resolved.value(QStringLiteral("duration")).toInt(), 333);
    }

    // ─── User preset library ──────────────────────────────────────────────

    void addUserPreset_writesFileWithSlugFilename()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        QVERIFY(c.addUserPreset(
            QStringLiteral("My Snappy Spring!"),
            {{QStringLiteral("curve"), QStringLiteral("spring:14.0,0.7")}, {QStringLiteral("duration"), 200}}));
        QCOMPARE(spy.count(), 1);

        // Slugified filename: lowercase, non-alnum collapsed to '-',
        // trailing '-' trimmed.
        QVERIFY(QFileInfo::exists(tmp.path() + QStringLiteral("/my-snappy-spring.json")));
    }

    void userPresets_excludesPathNamedFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Preset file
        QVERIFY(
            c.addUserPreset(QStringLiteral("My Curve"), {{QStringLiteral("curve"), QStringLiteral("0.5,0,0.5,1")}}));
        // Override file at a known path
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 250}}));

        // userPresets sees ONLY the preset, not the override
        const QVariantList presets = c.userPresets();
        QCOMPARE(presets.size(), 1);
        QCOMPARE(presets.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("My Curve"));
    }

    void addUserPreset_rejectsKnownPathNames()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        // "zone.snapIn" is a known event path — would shadow the
        // override slot.
        QVERIFY(!c.addUserPreset(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 100}}));
        QCOMPARE(spy.count(), 0);
        QVERIFY(c.userPresets().isEmpty());
    }

    void addUserPreset_rejectsEmptyAndAllSymbol()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(!c.addUserPreset(QString(), {{QStringLiteral("duration"), 100}}));
        // All-symbol slugifies to empty → reject (would write to ".json")
        QVERIFY(!c.addUserPreset(QStringLiteral("@@@"), {{QStringLiteral("duration"), 100}}));
    }

    void removeUserPreset_emitsAndDeletes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(
            c.addUserPreset(QStringLiteral("To Delete"), {{QStringLiteral("curve"), QStringLiteral("spring:10,0.5")}}));
        QCOMPARE(c.userPresets().size(), 1);

        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        QVERIFY(c.removeUserPreset(QStringLiteral("To Delete")));
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.userPresets().isEmpty());
    }

    void removeUserPreset_unknownReturnsFalse()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        QVERIFY(!c.removeUserPreset(QStringLiteral("nonexistent")));
        QCOMPARE(spy.count(), 0);
    }

    void resolvedProfile_partialLeafFillsFromParentAndDefaults()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Parent supplies curve only; leaf supplies duration only.
        // Resolved should have BOTH from their respective sources, plus
        // library defaults for everything else.
        QVERIFY(c.setOverride(QStringLiteral("zone"), {{QStringLiteral("curve"), QStringLiteral("spring:14.0,0.6")}}));
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 444}}));

        const QVariantMap resolved = c.resolvedProfile(QStringLiteral("zone.snapIn"));
        QCOMPARE(resolved.value(QStringLiteral("duration")).toInt(), 444);
        QCOMPARE(resolved.value(QStringLiteral("curve")).toString(), QStringLiteral("spring:14.0,0.6"));
        // Library default for the unspecified field:
        QCOMPARE(resolved.value(QStringLiteral("minDistance")).toInt(), PhosphorAnimation::Profile::DefaultMinDistance);
    }
};

QTEST_MAIN(TestAnimationsPageController)
#include "test_animations_page_controller.moc"
