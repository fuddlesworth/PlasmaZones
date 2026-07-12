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
 *   - updateSet renames and edits the description in one write, refusing a
 *     collision rather than destroying the other set
 *   - saveCurrentAsSet refuses to overwrite an existing name (decoration has
 *     no file-snapshot hook, so an overwrite would be unrecoverable)
 *   - export / import round-trip, including the file:// URL form the drop
 *     zone hands over, with a colliding import landing under a free name
 *   - the version gate refuses a set from a newer build
 *   - import and apply validate against the surface taxonomy, rejecting a
 *     whole set atomically on any unknown path
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

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include "config/configdefaults.h"
#include "settings/decorationpagecontroller.h"
#include "settings/shadersetstore.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;

namespace {

/// StubSettings that genuinely stores the decoration tree (the base stub's
/// setter is a no-op) and emits decorationProfileTreeChanged on a real change,
/// so the controller's write-back path is observable.
class TreeStubSettings : public StubSettings
{
public:
    using StubSettings::StubSettings;

    PhosphorSurfaceShaders::DecorationProfileTree decorationProfileTree() const override
    {
        return m_tree;
    }
    void setDecorationProfileTree(const PhosphorSurfaceShaders::DecorationProfileTree& tree) override
    {
        if (m_tree == tree)
            return;
        m_tree = tree;
        Q_EMIT decorationProfileTreeChanged();
        Q_EMIT settingsChanged();
    }

private:
    PhosphorSurfaceShaders::DecorationProfileTree m_tree;
};

/// Absolute path to the decoration-sets directory the store writes to,
/// recomputed the way DecorationPageController does. Valid only under
/// QStandardPaths test mode (see initTestCase).
QString decorationSetsDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userDecorationSetsSubdir());
}

/// The row for @p name, or an empty map when no such set is listed. Callers
/// QVERIFY the result is non-empty BEFORE asserting on a field: a
/// `for (row : sets) if (row.name == X) QVERIFY(...)` idiom silently runs zero
/// assertions when the row is absent, so the test would pass vacuously.
QVariantMap rowFor(ShaderSetStore* sets, const QString& name)
{
    const QVariantList rows = sets->availableSets();
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("name")).toString() == name)
            return map;
    }
    return QVariantMap{};
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
    }

    /// Each test starts from an empty sets directory.
    void init()
    {
        QDir(decorationSetsDir()).removeRecursively();
    }

    void cleanupTestCase()
    {
        QDir(decorationSetsDir()).removeRecursively();
    }

    // ─── Save / list / apply / remove ───────────────────────────────────────

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
    /// on screen. The store coalesces the emission onto the event loop, so the
    /// test has to spin it.
    void decorationSets_liveEditRefreshesRows()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Live"), QString()));

        QSignalSpy spy(sets, &ShaderSetStore::setsChanged);
        c.setChain(QStringLiteral("window.snapped"), QStringList{QStringLiteral("border")});
        QVERIFY2(spy.wait(1000), "a live tree edit must refresh the set rows");
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

        // A set with no version at all reads as the current format.
        const QString versionless = dir.filePath(QStringLiteral("versionless.json"));
        QJsonObject noVersion = validSetPayload(QStringLiteral("Versionless"));
        noVersion.remove(QStringLiteral("version"));
        writeSetFile(versionless, noVersion);
        QVERIFY2(sets->importSet(versionless), "a versionless set must read as the current format");
    }

    // ─── Refusals ───────────────────────────────────────────────────────────

    /// Saving onto an existing name would destroy that set, and decoration
    /// wires no file-snapshot hook, so nothing could restore it. Refuse.
    void saveDecorationSet_refusesToOverwriteExistingName()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("first")));

        // A different look, saved under the same name.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Taken"), QStringLiteral("second")),
                 "saving onto an existing set name must be refused");
        QCOMPARE(toastSpy.count(), 1);

        // The original set is intact — same description, same payload.
        const QVariantMap row = rowFor(sets, QStringLiteral("Taken"));
        QVERIFY(!row.isEmpty());
        QCOMPARE(row.value(QStringLiteral("description")).toString(), QStringLiteral("first"));
        QVERIFY(sets->applySet(QStringLiteral("Taken")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));
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
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Nothing"), QString()),
                 "saving an empty decoration tree must be refused");
        QCOMPARE(setsSpy.count(), 0);
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// saveCurrentAsSet rejects an empty name (would slugify to an empty
    /// filename).
    void saveDecorationSet_emptyNameRejected()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setChain(QString(), QStringList{QStringLiteral("border")});
        QVERIFY(!c.setsBridge()->saveCurrentAsSet(QString(), QString()));
    }

    /// applySet validates every entry up-front and rejects the whole set on any
    /// unknown surface path, leaving the current tree untouched (atomic apply —
    /// no partial write).
    void applyDecorationSet_unknownPathRejectsWholeSetAtomically()
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
