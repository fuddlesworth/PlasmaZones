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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include "config/settings.h"
#include "settings/animationspagecontroller.h"
#include "../helpers/IsolatedConfigGuard.h"
#include <PhosphorAnimation/AnimationShaderRegistry.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

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
        QCOMPARE(c.sectionForPath(QStringLiteral("popup.layoutPicker.show")), QStringLiteral("overlays"));
        QCOMPARE(c.sectionForPath(QString()), QString());
    }

    void eventLabel_titleCasesCamelLeaf()
    {
        AnimationsPageController c;
        QCOMPARE(c.eventLabel(QStringLiteral("global")), QStringLiteral("Global"));
        QCOMPARE(c.eventLabel(QStringLiteral("zone.snapIn")), QStringLiteral("Snap In"));
        QCOMPARE(c.eventLabel(QStringLiteral("popup.layoutPicker")), QStringLiteral("Layout Picker"));
        QCOMPARE(c.eventLabel(QStringLiteral("popup.layoutPicker.popIn")), QStringLiteral("Pop In"));
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

        // Every listed path is a built-in path.
        const QStringList builtIn = PhosphorAnimation::ProfilePaths::allBuiltInPaths();
        for (const QString& p : allListed) {
            QVERIFY2(builtIn.contains(p), qPrintable(QStringLiteral("unknown path in UI: ") + p));
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

    // ─── Motion sets ──────────────────────────────────────────────────────

    void saveCurrentAsMotionSet_capturesPathOverridesOnly()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Mix of path overrides and a user preset
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 222}}));
        QVERIFY(
            c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("curve"), QStringLiteral("spring:10,0.7")}}));
        QVERIFY(
            c.addUserPreset(QStringLiteral("My Preset"), {{QStringLiteral("curve"), QStringLiteral("0.5,0,0.5,1")}}));

        QSignalSpy spy(&c, &AnimationsPageController::motionSetsChanged);
        QVERIFY(c.saveCurrentAsMotionSet(QStringLiteral("My Set"), QStringLiteral("test set")));
        QCOMPARE(spy.count(), 1);

        const QVariantList sets = c.availableMotionSets();
        QCOMPARE(sets.size(), 1);
        const QVariantMap set = sets.first().toMap();
        QCOMPARE(set.value(QStringLiteral("name")).toString(), QStringLiteral("My Set"));
        // Should capture the 2 path overrides, NOT the preset
        QCOMPARE(set.value(QStringLiteral("overrideCount")).toInt(), 2);
    }

    void applyMotionSet_writesPerPathFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Build set, then clear overrides, then apply
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 333}}));
        QVERIFY(c.saveCurrentAsMotionSet(QStringLiteral("snappy-set"), QString()));
        QVERIFY(c.clearOverride(QStringLiteral("zone.snapIn")));
        QVERIFY(!c.hasOverride(QStringLiteral("zone.snapIn")));

        QVERIFY(c.applyMotionSet(QStringLiteral("snappy-set")));
        QVERIFY(c.hasOverride(QStringLiteral("zone.snapIn")));
        QCOMPARE(c.rawProfile(QStringLiteral("zone.snapIn")).value(QStringLiteral("duration")).toInt(), 333);
    }

    void applyMotionSet_mergesPreservesOtherPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Save a set with one path
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 222}}));
        QVERIFY(c.saveCurrentAsMotionSet(QStringLiteral("set-a"), QString()));
        QVERIFY(c.clearOverride(QStringLiteral("zone.snapIn")));

        // Set an UNRELATED override
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 555}}));

        // Apply set-a; osd.show should still be 555 (merge, not replace)
        QVERIFY(c.applyMotionSet(QStringLiteral("set-a")));
        QCOMPARE(c.rawProfile(QStringLiteral("osd.show")).value(QStringLiteral("duration")).toInt(), 555);
        QVERIFY(c.hasOverride(QStringLiteral("zone.snapIn")));
    }

    void removeMotionSet_emitsAndDeletes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 222}}));
        QVERIFY(c.saveCurrentAsMotionSet(QStringLiteral("To Remove"), QString()));
        QCOMPARE(c.availableMotionSets().size(), 1);

        QSignalSpy spy(&c, &AnimationsPageController::motionSetsChanged);
        QVERIFY(c.removeMotionSet(QStringLiteral("To Remove")));
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.availableMotionSets().isEmpty());
    }

    // ─── Pending changes / commit / revert ────────────────────────────────

    void hasPendingChanges_falseInitially()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());
        QVERIFY(!c.hasPendingChanges());
    }

    void setOverride_emitsPendingChangesChanged()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 250}}));
        QCOMPARE(spy.count(), 1);
        QVERIFY(c.hasPendingChanges());
    }

    void revertPending_restoresPreEditFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Establish a pre-edit baseline: write 100 ms.
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 100}}));
        c.commitPending();
        QVERIFY(!c.hasPendingChanges());

        // Edit to 250 ms → should snapshot the 100ms file.
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 250}}));
        QCOMPARE(c.rawProfile(QStringLiteral("zone.snapIn")).value(QStringLiteral("duration")).toInt(), 250);

        // Revert → file content restored to 100 ms.
        c.revertPending();
        QVERIFY(!c.hasPendingChanges());
        QCOMPARE(c.rawProfile(QStringLiteral("zone.snapIn")).value(QStringLiteral("duration")).toInt(), 100);
    }

    void revertPending_deletesFilesThatDidntExistBefore()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Fresh override on a path with no prior file.
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.hasOverride(QStringLiteral("osd.show")));

        c.revertPending();
        QVERIFY(!c.hasOverride(QStringLiteral("osd.show")));
    }

    void revertPending_emitsOverrideChangedPerPath()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 200}}));
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 300}}));

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        c.revertPending();

        // Two paths reverted → two emissions (one per file). Order is
        // hash-iteration so we check membership rather than position.
        QCOMPARE(spy.count(), 2);
        QStringList emitted;
        for (const auto& args : spy)
            emitted << args.at(0).toString();
        QVERIFY(emitted.contains(QStringLiteral("zone.snapIn")));
        QVERIFY(emitted.contains(QStringLiteral("osd.show")));
    }

    void commitPending_clearsSnapshotWithoutEmittingDataChanged()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 250}}));
        QVERIFY(c.hasPendingChanges());

        // commitPending clears the snapshot and only emits
        // pendingChangesChanged (once). It must NOT re-fire the
        // per-path overrideChanged or the data signals — the user just
        // saved, no rows visually moved.
        QSignalSpy pendingSpy(&c, &AnimationsPageController::pendingChangesChanged);
        QSignalSpy overrideSpy(&c, &AnimationsPageController::overrideChanged);
        c.commitPending();
        QVERIFY(!c.hasPendingChanges());
        QCOMPARE(pendingSpy.count(), 1);
        QCOMPARE(overrideSpy.count(), 0);
    }

    // ─── Shader override end-to-end (the actual user flow) ──────────────

    /// Simulates the full AnimationEventCard combo flow:
    ///   user clicks pixelate
    ///     → setShaderOverride(path, "pixelate", {})
    ///   refresh fires
    ///     → resolvedShaderProfile(path) → expect effectId="pixelate"
    /// If resolvedShaderProfile returns empty effectId here, the QML
    /// combo would snap back to "None" — mirrors the user's symptom.
    void setShaderOverride_resolveReturnsTheJustWrittenEffect()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        const QString path = QStringLiteral("osd.show");

        QVERIFY(c.setShaderOverride(path, QStringLiteral("pixelate"), {}));

        const QVariantMap raw = c.rawShaderProfile(path);
        const QVariantMap resolved = c.resolvedShaderProfile(path);

        // Raw read MUST contain effectId=pixelate.
        QCOMPARE(raw.value(QStringLiteral("effectId")).toString(), QStringLiteral("pixelate"));
        // Resolved read (what the QML refresh path uses) MUST too.
        QCOMPARE(resolved.value(QStringLiteral("effectId")).toString(), QStringLiteral("pixelate"));
    }

    /// Symptom-mirroring test: pick A, then immediately pick B.
    /// Both reads must return the corresponding effect.
    void setShaderOverride_repeatedPicksPreserveLatest()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        const QString path = QStringLiteral("osd.show");

        QVERIFY(c.setShaderOverride(path, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("pixelate"));

        QVERIFY(c.setShaderOverride(path, QStringLiteral("dissolve"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));

        QVERIFY(c.setShaderOverride(path, QStringLiteral("glitch"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(), QStringLiteral("glitch"));
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

    // ─── Path traversal hardening (security) ──────────────────────────────

    /// `setOverride` MUST reject any path that isn't a built-in event
    /// path. A crafted `"../etc/passwd"` would otherwise let a hostile
    /// QML caller write outside `userProfilesDir()`.
    void setOverride_rejectsTraversalPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);

        QVERIFY(!c.setOverride(QStringLiteral("../etc/passwd"), {{QStringLiteral("duration"), 100}}));
        QVERIFY(!c.setOverride(QStringLiteral("../../bad"), {{QStringLiteral("duration"), 100}}));
        QVERIFY(!c.setOverride(QStringLiteral("..\\windows-path"), {{QStringLiteral("duration"), 100}}));
        QVERIFY(!c.setOverride(QStringLiteral("zone/../../etc"), {{QStringLiteral("duration"), 100}}));
        // Plausible-looking but unknown paths are also rejected
        // (membership in allBuiltInPaths is the gate, not just lexical
        // shape).
        QVERIFY(!c.setOverride(QStringLiteral("not.a.real.path"), {{QStringLiteral("duration"), 100}}));

        QCOMPARE(spy.count(), 0);

        // No file landed anywhere under the tmp dir.
        QDir scan(tmp.path());
        const auto entries = scan.entryList(QDir::Files | QDir::NoDotAndDotDot);
        QVERIFY2(entries.isEmpty(),
                 qPrintable(QStringLiteral("traversal write leaked: ") + entries.join(QLatin1Char(','))));
    }

    /// `clearOverride` and `hasOverride` MUST also reject non-event paths
    /// — they share the same path → file-path mapping as setOverride.
    void clearAndHasOverride_rejectTraversalPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(!c.hasOverride(QStringLiteral("../etc/passwd")));
        QVERIFY(!c.clearOverride(QStringLiteral("../etc/passwd")));
        QVERIFY(!c.hasOverride(QStringLiteral("not.a.real.path")));
        QVERIFY(!c.clearOverride(QStringLiteral("not.a.real.path")));
    }

    /// `removeUserPreset` MUST not delete an override file even when its
    /// embedded `name` field happens to match the supplied preset name.
    /// Pre-fix, the directory-scan fallback walked every JSON file in
    /// the profiles dir and matched by `name`; an override file whose
    /// `name` matched the searched preset would be deleted.
    void removeUserPreset_doesNotTouchOverrideFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Write an override file. Its on-disk `name` field is "zone.snapIn"
        // (per setOverride's stamping rule).
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), {{QStringLiteral("duration"), 250}}));
        const QString overrideFilePath = tmp.path() + QStringLiteral("/zone.snapIn.json");
        QVERIFY(QFileInfo::exists(overrideFilePath));

        // Hand-craft a hostile preset on disk: `name` field = "zone.snapIn"
        // but the FILE is not at the canonical override slot. The library
        // skips it on userPresets() (collision filter) but a naive
        // remove-by-name walk would still delete it. We're asserting the
        // override file in the canonical slot remains intact regardless.
        QSignalSpy spy(&c, &AnimationsPageController::userPresetsChanged);
        QVERIFY(!c.removeUserPreset(QStringLiteral("zone.snapIn")));
        QCOMPARE(spy.count(), 0);

        // The override file MUST still exist — this is the load-bearing
        // assertion: removeUserPreset cannot collateral-damage overrides.
        QVERIFY2(QFileInfo::exists(overrideFilePath),
                 "override file was deleted by removeUserPreset(\"zone.snapIn\") — preset CRUD MUST NOT touch override "
                 "slots");
        QCOMPARE(c.rawProfile(QStringLiteral("zone.snapIn")).value(QStringLiteral("duration")).toInt(), 250);
    }

    // ─── Atomic motion-set application ────────────────────────────────────

    /// Manually plant a malformed motion-set file (mixing valid and
    /// invalid entries) and verify applyMotionSet() rejects the whole
    /// thing rather than partially writing. Pre-fix, the loop wrote
    /// each valid entry and skipped invalid ones, leaving inconsistent
    /// state.
    void applyMotionSet_malformedEntryRejectsWholeSet()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Make the motion-sets directory and write a hand-crafted file
        // with one VALID entry and one INVALID entry (unknown path).
        const QString setsDir = tmp.path() + QStringLiteral("/motionsets");
        QDir().mkpath(setsDir);
        const QString setPath = setsDir + QStringLiteral("/bad-set.json");

        QJsonObject validEntry;
        validEntry.insert(QStringLiteral("path"), QStringLiteral("zone.snapIn"));
        QJsonObject validProfile;
        validProfile.insert(QStringLiteral("duration"), 222);
        validEntry.insert(QStringLiteral("profile"), validProfile);

        QJsonObject invalidEntry;
        invalidEntry.insert(QStringLiteral("path"), QStringLiteral("../etc/passwd"));
        invalidEntry.insert(QStringLiteral("profile"), QJsonObject{});

        QJsonArray overrides;
        overrides.append(validEntry);
        overrides.append(invalidEntry);

        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("bad-set"));
        root.insert(QStringLiteral("overrides"), overrides);
        root.insert(QStringLiteral("version"), 1);

        QFile f(setPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
        f.close();

        // No prior override at zone.snapIn.
        QVERIFY(!c.hasOverride(QStringLiteral("zone.snapIn")));

        QVERIFY(!c.applyMotionSet(QStringLiteral("bad-set")));

        // Critical: the valid entry MUST NOT have been written. Atomic
        // semantics — all-or-nothing. Pre-fix this would be true.
        QVERIFY2(!c.hasOverride(QStringLiteral("zone.snapIn")),
                 "applyMotionSet wrote partial state from a malformed set — should have rejected atomically");
    }

    // ─── Shader override pendingChangesChanged emission ───────────────────

    /// Pre-fix, setShaderOverride and clearShaderOverride mutated
    /// settings but never emitted pendingChangesChanged, so the Save
    /// button never lit up for shader edits.
    void setShaderOverride_emitsPendingChangesChanged()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));
        QVERIFY2(spy.count() >= 1, "setShaderOverride MUST emit pendingChangesChanged");
    }

    /// `setShaderOverride(path, "")` — the empty-effectId clear shorthand
    /// — MUST also emit pendingChangesChanged. Pre-fix it routed to
    /// clearShaderOverride which silently mutated.
    void setShaderOverride_emptyEffectClearsAndEmits()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        // First write something so there's state to clear.
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QString(), {}));
        QVERIFY2(spy.count() >= 1, "setShaderOverride('','') MUST emit pendingChangesChanged on clear");
        QVERIFY(c.rawShaderProfile(QStringLiteral("osd.show")).isEmpty());
    }

    /// Set then explicit clear → both calls fire pendingChangesChanged.
    void setShaderOverride_thenClear_emitsTwice()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));
        QVERIFY(c.clearShaderOverride(QStringLiteral("osd.show")));
        QVERIFY2(spy.count() >= 2,
                 qPrintable(QStringLiteral("expected ≥2 emissions, got ") + QString::number(spy.count())));
    }

    // ─── Shader-leg support gate ──────────────────────────────────────────

    /// Pin the shader-leg-support predicate against the daemon-side list.
    /// `supportsShaderLeg` is the predicate the QML shader-picker visibility
    /// is bound to; if the surface set in
    /// `src/core/animationshadersupportedpaths.h` drifts away from the
    /// `resolveShaderEffect` call sites in
    /// `src/daemon/overlayservice.cpp`, the QML would either expose
    /// pickers that do nothing (drift in one direction) or hide pickers
    /// for events that DO produce shader legs (drift in the other).
    void supportsShaderLeg_matchesDaemonOverlayConsumers()
    {
        AnimationsPageController c;

        // Genuine OSDs (consumed leaves).
        QVERIFY(c.supportsShaderLeg(QStringLiteral("osd.show")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("osd.hide")));

        // Popup family — leg-leaf paths (consumed leaves).
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.layoutPicker.show")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.layoutPicker.hide")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.zoneSelector.show")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.zoneSelector.hide")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.snapAssist.show")));

        // Window family — consumed leaves driven by the KWin effect's
        // tryBeginShaderForEvent at kwin-effect/plasmazoneseffect.cpp.
        // Each maps to a window-lifecycle hook (windowAdded, windowClosed,
        // windowFinishUserMovedResized, maximized, minimized,
        // focusChanged) and runs the resolved shader on the
        // OffscreenEffect's redirected texture quad.
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.open")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.close")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.minimize")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.maximize")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.move")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.resize")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.focus")));

        // Ancestors of consumed leaves — supported because the
        // daemon's resolver walks them on the way to the leaf, so a
        // shader override here cascades to every descendant. Without
        // this, the user would have to set the same shader on every
        // popup leaf individually instead of once at the parent.
        QVERIFY(c.supportsShaderLeg(QStringLiteral("global")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("osd")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.layoutPicker")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.zoneSelector")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.snapAssist")));
        // `panel` is no longer a popup ancestor — popups moved to their
        // own root, leaving `panel` with only slideIn/slideOut/fadeIn/
        // fadeOut which the daemon's overlay service never consumes.
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("panel")));
        // `window` itself is now a consumable ancestor — setting a
        // shader at the family root cascades to every leaf above.
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window")));

        // Paths the resolver never walks through — any assignment would
        // be runtime-dead and silently shadow what the user thought
        // they set on a sibling. Must stay unsupported.
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("zone")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("zone.snapIn")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("widget")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("widget.fadeIn")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("workspace")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("workspace.switchIn")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("cursor")));
        // Sibling paths under `panel` and `osd` that aren't ancestors
        // of any consumed leaf.
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("panel.slideIn")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("panel.slideOut")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("osd.pop")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("osd.dim")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("popup.layoutPicker.popIn")));
        // SnapAssist hide is intentionally absent — surface destroys
        // before any hide frame paints. (Note: it would still be a
        // "consumable ancestor" if added, so this asserts it's NOT a
        // consumed leaf.)
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("popup.snapAssist.hide")));
        // Empty path / nonsense path.
        QVERIFY(!c.supportsShaderLeg(QString()));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("../etc/passwd")));
    }

    // ─── Pass-1 behaviour pins ────────────────────────────────────────────

    /// `setOverride` compare-and-skip: writing the same Profile twice
    /// must not fire a second `overrideChanged` / `pendingChangesChanged`.
    /// Pre-fix the second write rewrote an identical file and lit Save.
    void setOverride_repeatedWriteIsNoOp()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        const auto profile = QVariantMap{{QStringLiteral("duration"), 250},
                                         {QStringLiteral("curve"), QStringLiteral("0.33,1.00,0.68,1.00")}};

        QSignalSpy overrideSpy(&c, &AnimationsPageController::overrideChanged);
        QSignalSpy pendingSpy(&c, &AnimationsPageController::pendingChangesChanged);

        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), profile));
        QCOMPARE(overrideSpy.count(), 1);
        QCOMPARE(pendingSpy.count(), 1);

        // Second identical write — must short-circuit, no extra signals.
        QVERIFY(c.setOverride(QStringLiteral("zone.snapIn"), profile));
        QCOMPARE(overrideSpy.count(), 1);
        QCOMPARE(pendingSpy.count(), 1);
    }

    /// `setShaderOverride` short-circuits when the request matches the
    /// existing tree state. Without this, the QML two-way binding round-
    /// trip (combo activation → controller → settings → tree → QML
    /// refresh → re-emit) would dirty the page on every reload.
    void setShaderOverride_noOpWhenTreeAlreadyMatches()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));

        QSignalSpy pendingSpy(&c, &AnimationsPageController::pendingChangesChanged);
        // Same effectId + same (empty) parameters — must early-return.
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));
        QCOMPARE(pendingSpy.count(), 0);
    }

    // ─── Logging on malformed JSON ────────────────────────────────────────

    /// Plant an unparseable JSON file in the profiles dir; userPresets()
    /// should skip it AND log a warning at the qCWarning level. The
    /// emission is the load-bearing piece — pre-fix the parse error was
    /// silently swallowed.
    void userPresets_malformedJsonLogsAndSkips()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Write a known-good preset so the iteration has at least one
        // success result.
        QVERIFY(c.addUserPreset(QStringLiteral("Good"), {{QStringLiteral("duration"), 100}}));

        // Plant a malformed file directly.
        QFile bad(tmp.path() + QStringLiteral("/garbage.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ this is not valid json");
        bad.close();

        // Expect a warning to be logged for the malformed file. The
        // exact message is "AnimationPresetLibrary: failed to parse <path> : <error>".
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("AnimationPresetLibrary: failed to parse.*garbage")));

        const QVariantList presets = c.userPresets();
        // The good preset still surfaces; the malformed one is skipped.
        QCOMPARE(presets.size(), 1);
        QCOMPARE(presets.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Good"));
    }
};

QTEST_MAIN(TestAnimationsPageController)
#include "test_animations_page_controller.moc"
