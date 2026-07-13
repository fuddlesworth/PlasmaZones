// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decoration_sets.cpp
 * @brief Decoration-set CRUD: the ShaderSetStore behind
 *        DecorationPageController::setsBridge().
 *
 * Split from test_decorationpagecontroller.cpp to keep each test file under
 * the project's 800-line guideline — the same split, for the same reason, as
 * the motion side's test_animations_motion_sets.cpp.
 *
 * A decoration set is every per-surface override, captured
 * as one JSON file under `<GenericDataLocation>/plasmazones/decorationsets`.
 * Pinned behaviour:
 *   - Save / list / apply / remove round-trip, with apply MERGING (surfaces the
 *     set does not cover keep their current chains)
 *   - `active` is a CONTAINMENT check that survives unrelated live overrides
 *   - updateSet round-trips a rename plus description, and export/import
 *     round-trips a set through a file
 *   - Live edits refresh the rows through ONE coalesced setsChanged
 *   - A set with one unknown path is rejected WHOLE (atomic apply)
 *
 * The refusal paths (validation, version gate, name rules, overwrite consent)
 * are pinned in test_decoration_sets_validation.cpp.
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
#include <QUrl>

#include "config/configdefaults.h"
#include "phosphor_i18n.h"
#include "settings/decorationpagecontroller.h"
#include "settings/shadersetstore.h"
#include "../helpers/DecorationSetHelpers.h"
#include "../helpers/SetRowHelpers.h"
#include "../helpers/TreeStubSettings.h"

using namespace PlasmaZones;

class TestDecorationSets : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Redirect GenericDataLocation to an isolated test tree so the set CRUD
    /// never touches the real ~/.local/share.
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // init() and cleanupTestCase() recursively delete this directory. Fail
        // loudly if it ever resolves outside the sandbox. wipeSetsDir() re-checks
        // independently, because cleanupTestCase runs even when this fails.
        QVERIFY2(decorationSetsDir().contains(QLatin1String("qttest")),
                 "refusing to run outside QStandardPaths test mode");
    }

    /// Each test starts from an empty sets directory.
    void init()
    {
        wipeSetsDir();
    }

    void cleanupTestCase()
    {
        wipeSetsDir();
    }

    // ─── Save / list / apply / remove ───────────────────────────────────────

    /// Apply MERGES: a surface the set does not cover keeps whatever chain it
    /// has. The file header pins this and the motion side has the same test, but
    /// the decoration path had none, so a regression to replace-semantics would
    /// have wiped every uncovered surface with nothing to catch it.
    void applySet_mergesAndPreservesUncoveredSurfaces()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        // A set covering exactly one surface.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("tiled-only"), QString()));
        c.clearOverride(QStringLiteral("window.tiled"));

        // An override on a surface the set says nothing about.
        c.setChain(QStringLiteral("osd"), QStringList{QStringLiteral("border")});

        QVERIFY(sets->applySet(QStringLiteral("tiled-only")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));
        // The load-bearing assertion: the uncovered surface survived.
        QCOMPARE(c.chainAt(QStringLiteral("osd")), (QStringList{QStringLiteral("border")}));
    }

    /// A decoration set snapshots the per-surface overrides to a
    /// JSON file, and applying it merges those overrides back into the current
    /// tree. Full round-trip: save a look, mutate the tree, apply, and confirm
    /// the saved chains are restored; then remove and confirm the listing empties.
    void decorationSets_saveListApplyRemoveRoundTrips()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();
        QVERIFY(sets);

        // Author a look. The tree's baseline is deliberately NOT part of a set:
        // no settings page binds it, so a captured one could never be undone.
        c.setChain(QString(), QStringList{QStringLiteral("border")});
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        c.setChain(QStringLiteral("osd"), QStringList{QStringLiteral("border")});

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("My Look"), QStringLiteral("a test look")));
        QCOMPARE(setsSpy.count(), 1);

        // The set appears in the listing with the expected summary (the two
        // overrides it captured, and NOT the baseline), and it is `active` because
        // it still matches the live tree.
        const QVariantMap set = rowFor(sets, QStringLiteral("My Look"));
        QVERIFY(!set.isEmpty());
        QCOMPARE(sets->availableSets().size(), 1);
        QCOMPARE(set.value(QStringLiteral("description")).toString(), QStringLiteral("a test look"));
        QCOMPARE(set.value(QStringLiteral("slug")).toString(), QStringLiteral("my-look"));
        QCOMPARE(set.value(QStringLiteral("coverageCount")).toInt(), 2);
        QCOMPARE(set.value(QStringLiteral("coverage")).toStringList(),
                 (QStringList{QStringLiteral("osd"), QStringLiteral("window")}));
        QVERIFY2(set.value(QStringLiteral("modified")).toDateTime().isValid(), "the row must carry the file mtime");
        QVERIFY2(set.value(QStringLiteral("active")).toBool(), "a just-saved set must read as active");

        // Mutate the live tree away from the saved look.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("border")}));
        QVERIFY2(!rowFor(sets, QStringLiteral("My Look")).value(QStringLiteral("active")).toBool(),
                 "editing the live tree away from the set must clear its active flag");

        // Apply restores the saved overrides. The baseline is untouched: a set
        // neither captures nor applies one.
        QVERIFY(sets->applySet(QStringLiteral("My Look")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));
        QCOMPARE(c.chainAt(QStringLiteral("osd")), (QStringList{QStringLiteral("border")}));
        QVERIFY2(rowFor(sets, QStringLiteral("My Look")).value(QStringLiteral("active")).toBool(),
                 "the set must read as active again right after applying it");

        // Remove empties the listing and fires the change signal.
        QSignalSpy removeSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(sets->removeSet(QStringLiteral("My Look")));
        QCOMPARE(removeSpy.count(), 1);
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// Apply MERGES, so an unrelated live override must not clear the badge:
    /// the set's own entries are all still live. An equality check (rather
    /// than containment) would wrongly read this as inactive.
    void decorationSets_activeSurvivesUnrelatedOverride()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Tiled Only"), QString()));

        // A surface the set does not cover — apply would have left it alone.
        c.setChain(QStringLiteral("osd"), QStringList{QStringLiteral("border")});

        QVERIFY2(rowFor(sets, QStringLiteral("Tiled Only")).value(QStringLiteral("active")).toBool(),
                 "an override outside the set's coverage must not clear its active flag");
    }

    /// The `active` flag is derived from live state, so a live edit made
    /// anywhere else must re-fire setsChanged — otherwise the badge goes stale
    /// on screen. The store COALESCES that emission onto the event loop (a bulk
    /// revert would otherwise re-walk the sets dir once per restored path), so
    /// this pins both halves: an edit does refresh, and a burst of edits
    /// refreshes exactly once.
    void decorationSets_liveEditRefreshesRowsCoalesced()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Live"), QString()));

        // Drain the notify the setChain above already queued. Without this the
        // spy below would catch THAT emission and the test would pass even with
        // the live-state connection severed. Assert the drain actually
        // delivered, so a future change that stops it cannot silently restore
        // the vacuity.
        QSignalSpy spy(sets, &ShaderSetStore::setsChanged);
        QTest::qWait(0);
        QCOMPARE(spy.count(), 1);
        spy.clear();
        // Two edits in one turn must collapse into a single refresh.
        c.setChain(QStringLiteral("window.snapped"), QStringList{QStringLiteral("border")});
        c.setChain(QStringLiteral("osd"), QStringList{QStringLiteral("border")});
        QCOMPARE(spy.count(), 0); // still queued, not yet delivered
        QVERIFY2(spy.wait(1000), "a live tree edit must refresh the set rows");
        // wait() returns on the FIRST emission, so drain the loop before
        // counting: without this a second, un-coalesced emission could still be
        // sitting in the queue and the count would pass by luck.
        QTest::qWait(0);
        QCOMPARE(spy.count(), 1);
    }

    // ─── updateSet ──────────────────────────────────────────────────────────

    /// updateSet keeps the payload while renaming and editing the description,
    /// and frees the old name. Renaming onto an existing set is refused rather
    /// than destroying it.
    void decorationSets_updateRoundTripsAndRefusesCollision()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Old Name"), QStringLiteral("keep me")));
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Other"), QString()));

        QVERIFY(sets->updateSet(QStringLiteral("Old Name"), QStringLiteral("New Name"), QStringLiteral("new words")));
        QCOMPARE(sets->availableSets().size(), 2);
        QVERIFY2(rowFor(sets, QStringLiteral("Old Name")).isEmpty(), "the old name must be freed");

        const QVariantMap renamed = rowFor(sets, QStringLiteral("New Name"));
        QVERIFY(!renamed.isEmpty());
        QCOMPARE(renamed.value(QStringLiteral("description")).toString(), QStringLiteral("new words"));

        // The payload survived the rename: applying restores the saved chain.
        QVERIFY(sets->applySet(QStringLiteral("New Name")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));

        // A same-name call is a description-only edit; an empty description
        // clears the field (stored as an absent key, like save).
        QVERIFY(sets->updateSet(QStringLiteral("New Name"), QStringLiteral("New Name"), QString()));
        const QVariantMap cleared = rowFor(sets, QStringLiteral("New Name"));
        QVERIFY(!cleared.isEmpty());
        QVERIFY(cleared.value(QStringLiteral("description")).toString().isEmpty());

        // Renaming onto a name that is taken must not clobber the other set.
        QVERIFY2(!sets->updateSet(QStringLiteral("New Name"), QStringLiteral("Other"), QString()),
                 "rename onto an existing set must be refused");
        QCOMPARE(sets->availableSets().size(), 2);
        QVERIFY(!rowFor(sets, QStringLiteral("New Name")).isEmpty());
        QVERIFY(!rowFor(sets, QStringLiteral("Other")).isEmpty());
    }

    // ─── Export / import ────────────────────────────────────────────────────

    /// Export writes a file that import reads back, in both the local-path and
    /// the file:// URL form (the drop zone hands over a URL). Importing while
    /// the original is still present must not overwrite it — the copy lands
    /// under a free name.
    void decorationSets_exportImportRoundTrips()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Portable"), QString()));

        QTemporaryDir exportDir;
        QVERIFY(exportDir.isValid());
        const QString exported = exportDir.filePath(QStringLiteral("portable.json"));
        QVERIFY(sets->exportSet(QStringLiteral("Portable"), exported));
        QVERIFY(QFile::exists(exported));

        // Re-importing alongside the original yields a second, distinct set
        // rather than overwriting the first.
        QVERIFY(sets->importSet(exported));
        QCOMPARE(sets->availableSets().size(), 2);
        QVERIFY2(!rowFor(sets, QStringLiteral("Portable (2)")).isEmpty(),
                 "a colliding import must land under a free name");

        // The drop zone hands over a file:// URL, not a local path.
        QVERIFY2(sets->importSet(QUrl::fromLocalFile(exported).toString()),
                 "importSet must accept the file:// URL form the drop zone emits");
        QCOMPARE(sets->availableSets().size(), 3);

        // Deleting everything and importing restores the set from the file alone.
        const QVariantList all = sets->availableSets();
        for (const QVariant& row : all)
            QVERIFY(sets->removeSet(row.toMap().value(QStringLiteral("name")).toString()));
        QVERIFY(sets->availableSets().isEmpty());

        QVERIFY(sets->importSet(exported));
        QCOMPARE(sets->availableSets().size(), 1);
        QVERIFY(!rowFor(sets, QStringLiteral("Portable")).isEmpty());

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QVERIFY(sets->applySet(QStringLiteral("Portable")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));
    }

    // ─── Refusals kept with the round-trip (they assert post-state on the
    //     same fixtures) ─────────────────────────────────────────────────────

    /// applySet validates every entry up-front and rejects the whole set on any
    /// unknown surface path, leaving the current tree untouched (atomic apply —
    /// no partial write).
    void applySet_unknownPathRejectsWholeSetAtomically()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setSetsDirOverride(decorationSetsDir());
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});

        // Hand-craft a set file mixing a valid and an unknown-path entry.
        QJsonObject validProfile;
        validProfile.insert(QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")});
        QJsonObject validEntry;
        validEntry.insert(QStringLiteral("path"), QStringLiteral("window.snapped"));
        validEntry.insert(QStringLiteral("profile"), validProfile);
        QJsonObject badEntry;
        badEntry.insert(QStringLiteral("path"), QStringLiteral("../etc/passwd"));
        // A REAL profile, so only the path check can reject this entry.
        badEntry.insert(QStringLiteral("profile"),
                        QJsonObject{{QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")}}});

        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("bad-set"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{validEntry, badEntry});
        writeSetFile(decorationSetsDir() + QStringLiteral("/bad-set.json"), root);

        QVERIFY2(!c.setsBridge()->applySet(QStringLiteral("bad-set")), "a set with an unknown path must be rejected");
        // The valid entry must NOT have been applied — atomic all-or-nothing.
        QVERIFY2(!c.hasOverride(QStringLiteral("window.snapped")), "applySet wrote partial state from a malformed set");
        // The pre-existing override is untouched.
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("border")}));
    }
};

QTEST_MAIN(TestDecorationSets)

#include "test_decoration_sets.moc"
