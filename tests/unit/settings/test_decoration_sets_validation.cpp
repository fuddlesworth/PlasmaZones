// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decoration_sets_validation.cpp
 * @brief Decoration-set REFUSAL paths.
 *
 * test_decoration_sets.cpp covers the round-trip (save, list, apply, merge,
 * update, export/import). This one covers everything the store and the
 * decoration validator refuse, and the reason each refusal reports.
 *
 * Pinned behaviour:
 *   - Whole-set discipline: one bad entry refuses the SET, atomically. An
 *     unknown surface path, a non-array `overrides`, a non-object `profile`,
 *     a profile that engages no field, a set carrying a BASELINE (no settings
 *     page can reach one, so an imported baseline could never be undone), and an
 *     empty payload are all refused rather than partially applied
 *   - A set written by a NEWER format version is refused, not silently truncated
 *   - Save refuses an unconfirmed overwrite but honours a confirmed one, and
 *     refuses a name that is empty or slugifies to nothing
 *   - canUseSetName (the rename dialog's Ok gate) accepts exactly what updateSet
 *     accepts, including a row listed under its filename stem
 *   - availableSets skips a file it could never address, rather than listing a
 *     row the user cannot apply, rename or delete
 *   - A write that cannot land toasts and lists nothing
 *   - Every refusal carries its own reason to the toast, and the tests assert the
 *     MESSAGE: the refusals are otherwise indistinguishable (false + one toast)
 *
 * Settings is a TreeStubSettings: a StubSettings that actually stores the tree
 * and emits decorationProfileTreeChanged, so the controller's read-mutate-write
 * loop round-trips without the real PhosphorConfig::Store.
 */

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include "config/configdefaults.h"
#include "phosphor_i18n.h"
#include "settings/pages/decorationpagecontroller.h"
#include "settings/stores/shadersetstore.h"
#include "../helpers/DecorationSetHelpers.h"
#include "../helpers/SetRowHelpers.h"
#include "../helpers/TreeStubSettings.h"

using namespace PlasmaZones;

class TestDecorationSetsValidation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // init() and cleanupTestCase() recursively delete this directory. Fail
        // loudly if it ever resolves outside the sandbox. wipeSetsDir() re-checks
        // independently, because cleanupTestCase runs even when this fails.
        QVERIFY2(decorationSetsDir().contains(QLatin1String("qttest")),
                 "refusing to run outside QStandardPaths test mode");
    }

    void init()
    {
        wipeSetsDir();
    }

    void cleanupTestCase()
    {
        wipeSetsDir();
    }

    /// fromJson silently drops unknown and wrong-typed keys, so an entry whose
    /// chain is a string parses to an all-inherit profile. Staging that would make
    /// the surface READ as overridden while changing nothing the user can see.
    void applySet_rejectsAnEntryWhoseProfileEngagesNothing()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        // A well-formed entry on a REAL surface, whose profile parses to nothing:
        // `chain` is a string where an array belongs.
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.tiled"));
        entry.insert(QStringLiteral("profile"), QJsonObject{{QStringLiteral("chain"), QStringLiteral("glow")}});
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("hollow"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});
        writeSetFile(decorationSetsDir() + QStringLiteral("/hollow.json"), root);

        QVERIFY2(!sets->applySet(QStringLiteral("hollow")),
                 "an override that engages no field must be refused, not staged as a phantom");
        QVERIFY2(!c.hasOverride(QStringLiteral("window.tiled")), "and nothing may be written to the tree");
    }

    /// A decoration set must not carry a baseline at all. The tree has one, and
    /// D-Bus can set it, but no settings page binds it — so a baseline arriving
    /// through an import could never be seen or cleared again. Refuse it, exactly
    /// as the motion domain does.
    void applySet_rejectsASetCarryingABaseline()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QString(), QStringList{QStringLiteral("border")});

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.tiled"));
        entry.insert(QStringLiteral("profile"),
                     QJsonObject{{QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")}}});
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("withbase"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});
        root.insert(QStringLiteral("baseline"),
                    QJsonObject{{QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")}}});
        writeSetFile(decorationSetsDir() + QStringLiteral("/withbase.json"), root);

        QVERIFY2(!sets->applySet(QStringLiteral("withbase")), "a set carrying a baseline must be refused");
        // Neither the baseline nor the surface it also named was touched.
        QCOMPARE(c.chainAt(QString()), (QStringList{QStringLiteral("border")}));
        QVERIFY(!c.hasOverride(QStringLiteral("window.tiled")));
    }

    /// A set the store itself writes never carries a baseline, even when the live
    /// tree has one.
    void saveCurrentAsSet_doesNotCaptureTheBaseline()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QString(), QStringList{QStringLiteral("border")});
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Look"), QString()));

        // Round-trips: what it wrote, it can also read back.
        QVERIFY2(sets->applySet(QStringLiteral("Look")), "a set the store wrote must be one it accepts");
        QCOMPARE(rowFor(sets, QStringLiteral("Look")).value(QStringLiteral("coverageCount")).toInt(), 1);
    }

    /// A `profile` that is not an object at all (a string, say) has its own
    /// refusal branch, distinct from the engages-nothing rule that catches a
    /// wrong-typed key INSIDE an object.
    void applySet_rejectsANonObjectProfile()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.tiled"));
        entry.insert(QStringLiteral("profile"), QStringLiteral("glow"));
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("stringy"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});
        writeSetFile(decorationSetsDir() + QStringLiteral("/stringy.json"), root);

        // Pin the BRANCH: the engages-nothing rule would refuse this too, with a
        // different warning, so the unmatched ignoreMessage is what fails this
        // test if the type check goes away.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("is not an object")));
        QVERIFY2(!sets->applySet(QStringLiteral("stringy")), "a non-object profile must refuse the whole set");
        QVERIFY(!c.hasOverride(QStringLiteral("window.tiled")));
    }

    /// A present-but-non-array `overrides` used to read as "no overrides", so a set
    /// could import and apply with everything it claimed silently dropped.
    void applySet_rejectsNonArrayOverrides()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("bent"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QStringLiteral("not an array"));
        writeSetFile(decorationSetsDir() + QStringLiteral("/bent.json"), root);

        // Pin the BRANCH, not just the outcome: with the guard deleted, a
        // non-array overrides reads as an empty array and the empty-set
        // rejection produces an identical false. The unmatched ignoreMessage
        // is what fails this test if the guard goes away.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("overrides are not an array")));
        QVERIFY2(!sets->applySet(QStringLiteral("bent")),
                 "a malformed overrides field must refuse the whole set, not read as no overrides");
    }

    /// Every mutator resolves a row by name to slugify(name) + ".json", so a file
    /// whose stem does not match cannot be applied, renamed or deleted. Listing it
    /// would show the user a row they can never act on.
    void availableSets_skipsAnUnaddressableFile()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        // Hand-placed: the stem is "My Set", but "My Set" slugifies to "my-set".
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("My Set"));
        root.insert(QStringLiteral("version"), 1);
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.tiled"));
        entry.insert(QStringLiteral("profile"),
                     QJsonObject{{QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")}}});
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});
        writeSetFile(decorationSetsDir() + QStringLiteral("/My Set.json"), root);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("skipping")));
        QVERIFY2(rowFor(sets, QStringLiteral("My Set")).isEmpty(), "an unaddressable file must not be listed");
        QVERIFY2(rowFor(sets, QStringLiteral("my-set")).isEmpty(), "and not under its slug either");
    }

    /// canUseSetName is the Ok-gate for the rename dialog, and the ONLY thing
    /// standing between the user and a dismissed dialog that silently drops their
    /// description edit. It has to agree with updateSet exactly.
    void canUseSetName_matchesWhatUpdateSetAccepts()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Mine"), QString()));
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Other"), QString()));

        QVERIFY2(!sets->canUseSetName(QString(), QStringLiteral("Mine")), "an empty name is refused");
        QVERIFY2(!sets->canUseSetName(QStringLiteral("   "), QStringLiteral("Mine")), "whitespace only is refused");
        QVERIFY2(!sets->canUseSetName(QStringLiteral("!!!"), QStringLiteral("Mine")),
                 "a name that slugifies to nothing is refused");
        QVERIFY2(!sets->canUseSetName(QStringLiteral("Other"), QStringLiteral("Mine")),
                 "a collision with a DIFFERENT set is refused");
        QVERIFY2(sets->canUseSetName(QStringLiteral("Mine"), QStringLiteral("Mine")),
                 "keeping your own name is a description-only edit, not a self-collision");
        QVERIFY2(sets->canUseSetName(QStringLiteral("Fresh"), QStringLiteral("Mine")), "a free name is accepted");

        // Now prove the predicate agrees with the mutator on every one of those.
        QVERIFY(!sets->updateSet(QStringLiteral("Mine"), QString(), QString()));
        QVERIFY(!sets->updateSet(QStringLiteral("Mine"), QStringLiteral("!!!"), QString()));
        QVERIFY(!sets->updateSet(QStringLiteral("Mine"), QStringLiteral("Other"), QString()));
        QVERIFY(sets->updateSet(QStringLiteral("Mine"), QStringLiteral("Mine"), QStringLiteral("desc only")));
        QVERIFY(sets->updateSet(QStringLiteral("Mine"), QStringLiteral("Fresh"), QString()));
    }

    /// A write that cannot land must fail loudly and list nothing. Provoked with a
    /// directory in the destination's place rather than by revoking permissions, so
    /// it runs as root too. CLAUDE.md's documented build flow is Docker, which runs
    /// as root, and a permissions-based test would silently QSKIP there.
    ///
    /// This does NOT pin the snapshot rollback: decoration wires no fileSnapshot
    /// hook, so nothing is ever staged here. The rollback is pinned on the motion
    /// side, which is the domain that stages.
    void failedSetWriteToastsAndListsNothing()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        // A DIRECTORY where the set file wants to be: QSaveFile cannot commit over
        // it, so the write cannot land whatever the permissions say.
        const QString destination = decorationSetsDir() + QStringLiteral("/doomed.json");
        QVERIFY(QDir().mkpath(destination));
        QVERIFY(QFileInfo(destination).isDir());

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not write")));
        // overwrite=true: the destination "exists" (it is the directory), so the
        // collision check would otherwise refuse before the write is even tried,
        // and the write is what this test is about.
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Doomed"), QString(), /*overwrite=*/true),
                 "a save whose write cannot land must fail");
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(), PhosphorI18n::tr("Could not write the set to disk."));

        // The directory is untouched, and no half-written set is listed.
        QVERIFY(QFileInfo(destination).isDir());
        QVERIFY2(rowFor(sets, QStringLiteral("Doomed")).isEmpty(), "no half-written set may be listed");
    }

    /// Import validates against the surface taxonomy, so a file carrying an
    /// unknown path (a motion set, say) is refused at the boundary rather than
    /// landing on disk and failing later at apply time.
    void decorationSets_importRejectsForeignPayload()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString foreign = dir.filePath(QStringLiteral("foreign.json"));

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.appearance.open")); // a motion path
        // A REAL profile: an empty one would be refused by the empty-profile
        // rule, and the path-taxonomy guard under test would never run.
        entry.insert(QStringLiteral("profile"),
                     QJsonObject{{QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")}}});
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("Foreign"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});
        writeSetFile(foreign, root);

        QVERIFY2(!sets->importSet(foreign), "a set whose paths are not decoration surfaces must be refused");
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// A set written by a NEWER build may carry fields this build drops on
    /// parse, so applying it would commit a silently truncated look. Both apply
    /// and import must refuse it. A non-numeric version is malformed and is
    /// refused for the same reason.
    void decorationSets_refusesNewerFormatVersion()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        // A future-format set already sitting in the sets dir.
        writeSetFile(decorationSetsDir() + QStringLiteral("/from-the-future.json"),
                     validSetPayload(QStringLiteral("from-the-future"), 2));
        QVERIFY2(!sets->applySet(QStringLiteral("from-the-future")), "a set from a newer build must not be applied");
        QVERIFY2(!c.hasOverride(QStringLiteral("window.tiled")), "the refused set must not have been staged");

        // The same set arriving through import.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString newer = dir.filePath(QStringLiteral("newer.json"));
        writeSetFile(newer, validSetPayload(QStringLiteral("Newer"), 2));
        QSignalSpy newerSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->importSet(newer), "a set from a newer build must not be imported");
        QCOMPARE(newerSpy.count(), 1);
        QCOMPARE(newerSpy.first().first().toString(),
                 PhosphorI18n::tr("That set was written by a newer version of PlasmaZones."));

        // A present-but-non-numeric version is malformed, not "version 1".
        const QString malformed = dir.filePath(QStringLiteral("malformed.json"));
        QJsonObject root = validSetPayload(QStringLiteral("Malformed"));
        root.insert(QStringLiteral("version"), QStringLiteral("1"));
        writeSetFile(malformed, root);
        QVERIFY2(!sets->importSet(malformed), "a non-numeric version must be refused");

        // A non-integral version is malformed too. QJsonValue::toInt() would
        // hand back the default for it, silently reading 1.5 as version 1.
        const QString fractional = dir.filePath(QStringLiteral("fractional.json"));
        QJsonObject frac = validSetPayload(QStringLiteral("Fractional"));
        frac.insert(QStringLiteral("version"), 1.5);
        writeSetFile(fractional, frac);
        QVERIFY2(!sets->importSet(fractional), "a non-integral version must be refused");

        // A set with no version at all reads as the current format.
        const QString versionless = dir.filePath(QStringLiteral("versionless.json"));
        QJsonObject noVersion = validSetPayload(QStringLiteral("Versionless"));
        noVersion.remove(QStringLiteral("version"));
        writeSetFile(versionless, noVersion);
        QVERIFY2(sets->importSet(versionless), "a versionless set must read as the current format");
    }

    /// Saving onto an existing name destroys that set, and decoration wires no
    /// file-snapshot hook, so nothing could restore it. An UNCONFIRMED save is
    /// therefore refused (with a toast) and leaves the stored set intact.
    /// Re-saving with the user's consent (overwrite=true) is how they update a
    /// set after tweaking their look, so that path must still work.
    void saveDecorationSet_refusesUnconfirmedOverwriteAndHonoursConsent()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("first")));
        QCOMPARE(sets->existingSetName(QStringLiteral("Taken")), QStringLiteral("Taken"));
        QVERIFY(sets->existingSetName(QStringLiteral("Not Taken")).isEmpty());

        // A different look, saved under the same name, WITHOUT consent.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("second")),
                 "an unconfirmed overwrite must be refused");
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(),
                 PhosphorI18n::tr("A set named \"%1\" already exists.").arg(QStringLiteral("Taken")));

        // The stored set is intact — same description, same payload.
        const QVariantMap row = rowFor(sets, QStringLiteral("Taken"));
        QVERIFY(!row.isEmpty());
        QCOMPARE(row.value(QStringLiteral("description")).toString(), QStringLiteral("first"));
        QVERIFY(sets->applySet(QStringLiteral("Taken")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));

        // Now WITH consent: the set is re-pointed at the new look.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QVERIFY2(sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("second"), true),
                 "a confirmed overwrite must be honoured");
        QCOMPARE(sets->availableSets().size(), 1);
        const QVariantMap updated = rowFor(sets, QStringLiteral("Taken"));
        QVERIFY(!updated.isEmpty());
        QCOMPARE(updated.value(QStringLiteral("description")).toString(), QStringLiteral("second"));

        // The re-saved payload is the NEW look.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->applySet(QStringLiteral("Taken")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("border")}));
    }

    /// applySet is fire-and-forget from QML, so every failure branch must carry
    /// its reason to the toast or the user just watches nothing happen. Covers
    /// the three branches reachable here (read / version / validation); the
    /// apply-failed branch needs a failing ISettings, and the mutation-guard
    /// branch is motion-only (decoration wires no guard).
    void applySet_toastsReadVersionAndValidationFailures()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        // Missing file.
        QSignalSpy missingSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY(!sets->applySet(QStringLiteral("nonexistent")));
        QCOMPARE(missingSpy.count(), 1);
        QCOMPARE(missingSpy.first().first().toString(),
                 PhosphorI18n::tr("Could not read the set \"%1\".").arg(QStringLiteral("nonexistent")));

        // Newer format version.
        writeSetFile(decorationSetsDir() + QStringLiteral("/newer.json"), validSetPayload(QStringLiteral("newer"), 2));
        QSignalSpy versionSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY(!sets->applySet(QStringLiteral("newer")));
        QCOMPARE(versionSpy.count(), 1);
        QCOMPARE(
            versionSpy.first().first().toString(),
            PhosphorI18n::tr("\"%1\" was written by a newer version of PlasmaZones.").arg(QStringLiteral("newer")));

        // Foreign payload (an event path, not a decoration surface).
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.appearance.open"));
        // A REAL profile: an empty one would be refused by the empty-profile
        // rule, and the path-taxonomy guard under test would never run.
        entry.insert(QStringLiteral("profile"),
                     QJsonObject{{QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")}}});
        QJsonObject foreign;
        foreign.insert(QStringLiteral("name"), QStringLiteral("foreign"));
        foreign.insert(QStringLiteral("version"), 1);
        foreign.insert(QStringLiteral("overrides"), QJsonArray{entry});
        writeSetFile(decorationSetsDir() + QStringLiteral("/foreign.json"), foreign);
        QSignalSpy validateSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY(!sets->applySet(QStringLiteral("foreign")));
        QCOMPARE(validateSpy.count(), 1);
        QCOMPARE(validateSpy.first().first().toString(),
                 PhosphorI18n::tr("\"%1\" does not match this page.").arg(QStringLiteral("foreign")));
    }

    /// An import carrying no entries would land as a row that applySet then
    /// refuses. It is named for what it is, rather than falling through to the
    /// taxonomy message, which would misdescribe an empty file as a foreign one.
    void decorationSets_importRejectsEmptySet()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString empty = dir.filePath(QStringLiteral("empty.json"));
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("Empty"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{});
        writeSetFile(empty, root);

        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->importSet(empty), "a set carrying nothing must be refused");
        QCOMPARE(toastSpy.count(), 1);
        // The taxonomy validator would refuse an empty set anyway, so the ONLY
        // thing the store's dedicated empty check buys is an honest message.
        // Assert it, or this test passes with that check deleted.
        QCOMPARE(toastSpy.first().first().toString(), PhosphorI18n::tr("That set is empty."));
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// A set file larger than the read cap is refused before it is slurped into
    /// memory. importSet takes a user-chosen path, so this is a boundary check.
    void decorationSets_importRejectsOversizedFile()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString huge = dir.filePath(QStringLiteral("huge.json"));

        // A VALID set, padded past the cap with a giant description. Garbage
        // bytes would prove nothing: the JSON parser would reject them even
        // with the cap removed, so the test could not tell the two apart.
        QJsonObject root = validSetPayload(QStringLiteral("Huge"));
        root.insert(QStringLiteral("description"), QString(5 * 1024 * 1024, QLatin1Char('x')));
        writeSetFile(huge, root);
        QVERIFY(QFileInfo(huge).size() > 4 * 1024 * 1024);

        QVERIFY2(!sets->importSet(huge), "a set file over the size cap must be refused");
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// Saving with an empty tree (no overrides) is refused: the
    /// resulting set would be a no-op that applySet then rejects, so it must
    /// never reach disk.
    void saveDecorationSet_emptyTreeRejected()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Nothing"), QString()),
                 "saving an empty decoration tree must be refused");
        QCOMPARE(setsSpy.count(), 0);
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(), PhosphorI18n::tr("There is nothing to capture yet."));
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// saveCurrentAsSet rejects an empty name (would slugify to an empty
    /// filename).
    void saveDecorationSet_emptyOrUnusableNameRejected()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();
        c.setChain(QString(), QStringList{QStringLiteral("border")});

        {
            QSignalSpy emptySpy(sets, &ShaderSetStore::toastRequested);
            QVERIFY(!sets->saveCurrentAsSet(QString(), QString()));
            QCOMPARE(emptySpy.count(), 1);
            QCOMPARE(emptySpy.first().first().toString(), PhosphorI18n::tr("A set needs a name."));
        }

        // A name with nothing a filename can be built from ("!!!" slugifies to
        // nothing). The Save button only checks for non-blank text, so this
        // reaches the store and must be refused WITH a reason — otherwise the
        // user clicks Save and watches nothing happen.
        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("!!!"), QString()),
                 "a name that slugifies to nothing must be refused");
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(),
                 PhosphorI18n::tr("That name cannot be used. Try one with letters or numbers in it."));
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// applySet / removeSet on a name with no file report failure rather than
    /// crashing or emitting a spurious change.
    void decorationSets_unknownNameReturnsFalse()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(!sets->applySet(QStringLiteral("nonexistent")));

        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("removeSet: no such set")));
        QVERIFY(!sets->removeSet(QStringLiteral("nonexistent")));
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(),
                 PhosphorI18n::tr("Could not delete \"%1\".").arg(QStringLiteral("nonexistent")));
        QCOMPARE(setsSpy.count(), 0);
    }
    /// Export is fire-and-forget from QML's side, so every failure has to carry
    /// its own reason to the toast. None of its refusal branches had any coverage.
    void exportSet_toastsEveryFailure()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Mine"), QString()));

        QSignalSpy emptyDest(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->exportSet(QStringLiteral("Mine"), QString()), "an empty destination must be refused");
        QCOMPARE(emptyDest.count(), 1);
        QCOMPARE(emptyDest.first().first().toString(), PhosphorI18n::tr("Could not write to that location."));

        // A directory where the export wants to write: the copy cannot land.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString blocked = tmp.path() + QStringLiteral("/blocked.json");
        QVERIFY(QDir().mkpath(blocked));

        QSignalSpy badDest(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->exportSet(QStringLiteral("Mine"), blocked), "a destination that cannot be written must fail");
        QCOMPARE(badDest.count(), 1);
        QCOMPARE(badDest.first().first().toString(), PhosphorI18n::tr("Could not write to %1.").arg(blocked));

        // A set that is not there at all: the source is not a regular file, which
        // is the same boundary readSetFile enforces.
        QSignalSpy missing(sets, &ShaderSetStore::toastRequested);
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("ShaderSetStore::exportSet: refusing to read")));
        QVERIFY2(!sets->exportSet(QStringLiteral("Nope"), tmp.path() + QStringLiteral("/out.json")),
                 "exporting a set that does not exist must fail");
        QCOMPARE(missing.count(), 1);
        QCOMPARE(missing.first().first().toString(),
                 PhosphorI18n::tr("Could not read the set \"%1\".").arg(QStringLiteral("Nope")));
    }

    /// The case canUseSetName's path compare exists for: a file whose stem IS a
    /// valid slug, but whose stored name is something else. availableSets lists it
    /// under the stem, so a name-based compare would report the row's own name as
    /// taken by somebody else and Ok could never enable.
    void canUseSetName_acceptsARowListedUnderItsStem()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.tiled"));
        entry.insert(QStringLiteral("profile"),
                     QJsonObject{{QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")}}});
        QJsonObject root;
        // Stem "my-set" is a valid slug, but the stored name is not it.
        root.insert(QStringLiteral("name"), QStringLiteral("Something Else"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});
        writeSetFile(decorationSetsDir() + QStringLiteral("/my-set.json"), root);

        // Listed under the stem, because that is the only name that resolves back
        // to this file.
        QVERIFY(!rowFor(sets, QStringLiteral("my-set")).isEmpty());

        QVERIFY2(sets->canUseSetName(QStringLiteral("my-set"), QStringLiteral("my-set")),
                 "the row's own name must be usable, or it can never be renamed or re-described");
        QVERIFY2(sets->updateSet(QStringLiteral("my-set"), QStringLiteral("my-set"), QStringLiteral("desc")),
                 "and the mutator must agree with the predicate");
    }
    /// A file that is not JSON at all must be refused with its own reason, not
    /// misreported as empty or as not matching the page.
    void importSet_refusesAnUnreadableFile()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString garbage = dir.filePath(QStringLiteral("garbage.json"));
        QFile f(garbage);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray bytes = "this is not json at all {";
        QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
        f.close();

        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("failed to parse")));
        QVERIFY2(!sets->importSet(garbage), "an unparseable file must be refused");
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(), PhosphorI18n::tr("That file is not a readable set."));
        QVERIFY(sets->availableSets().isEmpty());
    }
    /// An imported file whose stored name AND filename both slugify to nothing
    /// has no name the store could ever address it by. It is refused with its
    /// own reason rather than imported as an unreachable row.
    void importSet_refusesAnUnnameablePayload()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root = validSetPayload(QStringLiteral("!!!"));
        const QString payload = dir.filePath(QStringLiteral("!!!.json"));
        writeSetFile(payload, root);

        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->importSet(payload), "a payload with no usable name must be refused");
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(), PhosphorI18n::tr("That set has no usable name."));
        QVERIFY(sets->availableSets().isEmpty());
    }
};

QTEST_MAIN(TestDecorationSetsValidation)

#include "test_decoration_sets_validation.moc"
