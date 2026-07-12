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
 * A decoration set is the baseline plus every per-surface override, captured
 * as one JSON file under `<GenericDataLocation>/plasmazones/decorationsets`.
 * Pinned behaviour:
 *   - save / list / apply / remove round-trip, with apply MERGING (surfaces
 *     the set does not cover keep their current chains)
 *   - `active` is a CONTAINMENT check, not equality: a set stays active while
 *     unrelated overrides exist, because apply would have left them alone
 *   - a live edit made anywhere else refreshes the rows, and a burst of edits
 *     collapses into exactly one refresh (the store coalesces the notify)
 *   - updateSet renames and edits the description in one write, refusing a
 *     collision rather than destroying the other set
 *   - saveCurrentAsSet refuses an UNCONFIRMED overwrite (decoration has no
 *     file-snapshot hook, so it would be unrecoverable) but honours a
 *     confirmed one, which is how the user re-points a set at a new look
 *   - saving nothing, and saving under an empty name, are refused
 *   - export / import round-trip, including the file:// URL form the drop
 *     zone hands over, with a colliding import landing under a free name
 *   - the version gate refuses a newer, a non-numeric, and a non-integral
 *     version; import refuses an empty set and one over the read size cap
 *   - import and apply validate against the surface taxonomy, rejecting a
 *     whole set atomically on any unknown path, and apply toasts each failure
 *     reachable here (QML fires and forgets it)
 *
 * Settings is a TreeStubSettings: a StubSettings that actually stores the tree
 * and emits decorationProfileTreeChanged, so the controller's
 * read-mutate-write loop round-trips without the real PhosphorConfig::Store.
 */

#include <QSignalSpy>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include "config/configdefaults.h"
#include "phosphor_i18n.h"
#include "settings/decorationpagecontroller.h"
#include "settings/shadersetstore.h"
#include "../helpers/SetRowHelpers.h"
#include "../helpers/TreeStubSettings.h"

using namespace PlasmaZones;

namespace {

/// Absolute path to the decoration-sets directory the store writes to,
/// recomputed the way DecorationPageController does. Valid only under
/// QStandardPaths test mode (see initTestCase).
QString decorationSetsDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userDecorationSetsSubdir());
}

/// Write @p root to @p path as a hand-crafted set file.
void writeSetFile(const QString& path, const QJsonObject& root)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const QByteArray bytes = QJsonDocument(root).toJson();
    QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
    f.close();
}

/// A minimal valid decoration-set payload covering `window.tiled`.
QJsonObject validSetPayload(const QString& name, int version = 1)
{
    QJsonObject profile;
    profile.insert(QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")});
    QJsonObject entry;
    entry.insert(QStringLiteral("path"), QStringLiteral("window.tiled"));
    entry.insert(QStringLiteral("profile"), profile);

    QJsonObject root;
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("overrides"), QJsonArray{entry});
    return root;
}

/// Recursively clear the sets directory, but ONLY inside the QStandardPaths
/// sandbox. The guard lives here rather than in initTestCase because QtTest
/// still runs cleanupTestCase after initTestCase fails: a future edit that
/// dropped setTestModeEnabled would otherwise delete the user's real saved sets
/// on its way out.
void wipeSetsDir()
{
    const QString dir = decorationSetsDir();
    if (!dir.contains(QLatin1String("qttest"))) {
        qWarning("refusing to wipe a sets directory outside the test sandbox");
        return;
    }
    QDir(dir).removeRecursively();
}

} // namespace

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

    /// A decoration set snapshots the baseline + per-surface overrides to a
    /// JSON file, and applying it merges those overrides back into the current
    /// tree. Full round-trip: save a look, mutate the tree, apply, and confirm
    /// the saved chains are restored; then remove and confirm the listing empties.
    void decorationSets_saveListApplyRemoveRoundTrips()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();
        QVERIFY(sets);

        // Author a look: baseline "border", window.tiled → "glow".
        c.setChain(QString(), QStringList{QStringLiteral("border")});
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("My Look"), QStringLiteral("a test look")));
        QCOMPARE(setsSpy.count(), 1);

        // The set appears in the listing with the expected summary (baseline
        // counts as one covered surface, plus the single window.tiled
        // override), and it is `active` because it still matches the live tree.
        const QVariantMap set = rowFor(sets, QStringLiteral("My Look"));
        QVERIFY(!set.isEmpty());
        QCOMPARE(sets->availableSets().size(), 1);
        QCOMPARE(set.value(QStringLiteral("description")).toString(), QStringLiteral("a test look"));
        QCOMPARE(set.value(QStringLiteral("slug")).toString(), QStringLiteral("my-look"));
        QCOMPARE(set.value(QStringLiteral("coverageCount")).toInt(), 2);
        QCOMPARE(set.value(QStringLiteral("hasBaseline")).toBool(), true);
        QCOMPARE(set.value(QStringLiteral("coverage")).toStringList(), (QStringList{QStringLiteral("window")}));
        QVERIFY2(set.value(QStringLiteral("modified")).toDateTime().isValid(), "the row must carry the file mtime");
        QVERIFY2(set.value(QStringLiteral("active")).toBool(), "a just-saved set must read as active");

        // Mutate the live tree away from the saved look.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("border")}));
        QVERIFY2(!rowFor(sets, QStringLiteral("My Look")).value(QStringLiteral("active")).toBool(),
                 "editing the live tree away from the set must clear its active flag");

        // Apply restores the saved baseline + override.
        QVERIFY(sets->applySet(QStringLiteral("My Look")));
        QCOMPARE(c.chainAt(QString()), (QStringList{QStringLiteral("border")}));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));
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

    /// Import validates against the surface taxonomy, so a file carrying an
    /// unknown path (a motion set, say) is refused at the boundary rather than
    /// landing on disk and failing later at apply time.
    void decorationSets_importRejectsForeignPayload()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString foreign = dir.filePath(QStringLiteral("foreign.json"));

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.appearance.open")); // a motion path
        entry.insert(QStringLiteral("profile"), QJsonObject{});
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("Foreign"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});
        writeSetFile(foreign, root);

        QVERIFY2(!sets->importSet(foreign), "a set whose paths are not decoration surfaces must be refused");
        QVERIFY(sets->availableSets().isEmpty());
    }

    // ─── Version gate ───────────────────────────────────────────────────────

    /// A set written by a NEWER build may carry fields this build drops on
    /// parse, so applying it would commit a silently truncated look. Both apply
    /// and import must refuse it. A non-numeric version is malformed and is
    /// refused for the same reason.
    void decorationSets_refusesNewerFormatVersion()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
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
        QVERIFY2(!sets->importSet(newer), "a set from a newer build must not be imported");

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

    // ─── Refusals ───────────────────────────────────────────────────────────

    /// Saving onto an existing name destroys that set, and decoration wires no
    /// file-snapshot hook, so nothing could restore it. An UNCONFIRMED save is
    /// therefore refused (with a toast) and leaves the stored set intact.
    /// Re-saving with the user's consent (overwrite=true) is how they update a
    /// set after tweaking their look, so that path must still work.
    void saveDecorationSet_refusesUnconfirmedOverwriteAndHonoursConsent()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
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
        entry.insert(QStringLiteral("profile"), QJsonObject{});
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

    /// Saving with an empty tree (no baseline, no overrides) is refused: the
    /// resulting set would be a no-op that applySet then rejects, so it must
    /// never reach disk.
    void saveDecorationSet_emptyTreeRejected()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Nothing"), QString()),
                 "saving an empty decoration tree must be refused");
        QCOMPARE(setsSpy.count(), 0);
        QCOMPARE(toastSpy.count(), 1); // the refusal must say why
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// saveCurrentAsSet rejects an empty name (would slugify to an empty
    /// filename).
    void saveDecorationSet_emptyOrUnusableNameRejected()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();
        c.setChain(QString(), QStringList{QStringLiteral("border")});

        QVERIFY(!sets->saveCurrentAsSet(QString(), QString()));

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

    /// applySet validates every entry up-front and rejects the whole set on any
    /// unknown surface path, leaving the current tree untouched (atomic apply —
    /// no partial write).
    void applySet_unknownPathRejectsWholeSetAtomically()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});

        // Hand-craft a set file mixing a valid and an unknown-path entry.
        QJsonObject validProfile;
        validProfile.insert(QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")});
        QJsonObject validEntry;
        validEntry.insert(QStringLiteral("path"), QStringLiteral("window.snapped"));
        validEntry.insert(QStringLiteral("profile"), validProfile);
        QJsonObject badEntry;
        badEntry.insert(QStringLiteral("path"), QStringLiteral("../etc/passwd"));
        badEntry.insert(QStringLiteral("profile"), QJsonObject{});

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

    /// applySet / removeSet on a name with no file report failure rather than
    /// crashing or emitting a spurious change.
    void decorationSets_unknownNameReturnsFalse()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(!sets->applySet(QStringLiteral("nonexistent")));
        QVERIFY(!sets->removeSet(QStringLiteral("nonexistent")));
        QCOMPARE(setsSpy.count(), 0);
    }
};

QTEST_MAIN(TestDecorationSets)

#include "test_decoration_sets.moc"
