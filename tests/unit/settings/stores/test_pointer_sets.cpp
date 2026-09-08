// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_pointer_sets.cpp
 * @brief Pointer-set CRUD and refusals: the ShaderSetStore behind
 *        PointerPageController::setsBridge().
 *
 * A pointer set is the user's whole pointer chain captured as one JSON file
 * under `<GenericDataLocation>/plasmazones/pointersets`. Unlike the decoration
 * and motion domains there is no path taxonomy: the chain is a single value, so
 * a set always carries exactly ONE entry at the synthetic path "pointer" and
 * applying it REPLACES the chain rather than merging into it.
 *
 * Pinned behaviour:
 *   - Save / list / apply / remove round-trip, with apply replacing the WHOLE
 *     chain (layers the set does not mention are gone, not preserved)
 *   - `active` is true when live equals the set and false after any edit
 *   - Saving an empty chain is refused ("nothing to capture")
 *   - updateSet round-trips a rename plus description and refuses a collision
 *   - Export / import round-trip, including the free-name rule on collision
 *   - A set written by a NEWER format version is refused on apply and on import
 *   - Every validator refusal, one test slot each: a baseline key, a non-array
 *     `overrides`, zero entries, two entries, a wrong path, a non-object
 *     profile, and a profile that parses to an empty chain
 */

#include <QSignalSpy>
#include <QTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <PhosphorPointer/PointerProfile.h>

#include "config/configdefaults.h"
#include "phosphor_i18n.h"
#include "settings/pages/pointerpagecontroller.h"
#include "settings/stores/shadersetstore.h"
#include "helpers/SetRowHelpers.h"
#include "helpers/StubSettings.h"

using namespace PlasmaZones;
using PhosphorPointerShaders::PointerProfile;

namespace {

/// Absolute path to this binary's pointer-sets sandbox, under the
/// QStandardPaths test-mode tree. Suffixed with the application name for the
/// same reason the decoration helpers do it: parallel ctest must not have two
/// binaries wiping each other's in-flight files.
QString pointerSetsDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userPointerSetsSubdir() + QLatin1Char('-')
                           + QCoreApplication::applicationName());
}

/// Recursively clear the sets directory, but ONLY inside the QStandardPaths
/// sandbox. The guard lives here rather than in initTestCase because QtTest
/// still runs cleanupTestCase after initTestCase fails.
void wipePointerSetsDir()
{
    const QString dir = pointerSetsDir();
    if (!dir.contains(QLatin1String("qttest"))) {
        qWarning("refusing to wipe a sets directory outside the test sandbox");
        return;
    }
    QDir(dir).removeRecursively();
}

/// Write @p root to @p path as a hand-crafted set file.
void writePointerSetFile(const QString& path, const QJsonObject& root)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const QByteArray bytes = QJsonDocument(root).toJson();
    QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
    f.close();
}

/// One chain layer in the QVariantMap shape PointerPageController::setChain
/// consumes.
QVariantMap layerMap(const QString& effectId)
{
    QVariantMap m;
    m.insert(QStringLiteral("effectId"), effectId);
    m.insert(QStringLiteral("enabled"), true);
    return m;
}

/// A hand-built set envelope carrying @p overrides verbatim, so each refusal
/// test can hand the validator exactly the malformed shape it is pinning.
QJsonObject setRoot(const QString& name, const QJsonValue& overrides, int version = 1)
{
    QJsonObject root;
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("overrides"), overrides);
    return root;
}

/// A well-formed single entry at the pointer path, carrying @p profile.
QJsonObject entryWith(const QString& path, const QJsonValue& profile)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("path"), path);
    entry.insert(QStringLiteral("profile"), profile);
    return entry;
}

/// A real one-layer chain payload.
QJsonObject validProfileJson(const QString& effectId = QStringLiteral("comet"))
{
    QJsonObject layer;
    layer.insert(QStringLiteral("effectId"), effectId);
    layer.insert(QStringLiteral("enabled"), true);
    QJsonObject profile;
    profile.insert(QStringLiteral("layers"), QJsonArray{layer});
    return profile;
}

/// The chain's pack ids in paint order, read from the live settings rather than
/// the controller so the assertion cannot be satisfied by a stale cache.
QStringList chainIds(const StubSettings& settings)
{
    QStringList ids;
    const PointerProfile chain = settings.pointerChain();
    for (const auto& layer : chain.layers) {
        ids.append(layer.effectId);
    }
    return ids;
}

} // namespace

class TestPointerSets : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Redirect GenericDataLocation to an isolated test tree so the set CRUD
    /// never touches the real ~/.local/share.
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY2(pointerSetsDir().contains(QLatin1String("qttest")),
                 "refusing to run outside QStandardPaths test mode");
    }

    void init()
    {
        wipePointerSetsDir();
    }

    void cleanupTestCase()
    {
        wipePointerSetsDir();
    }

    // ─── Save / list / apply / remove ───────────────────────────────────────

    /// A pointer set snapshots the whole chain to a JSON file, and applying it
    /// puts that chain back. Full round-trip: save a look, mutate the chain,
    /// apply, then remove and confirm the listing empties.
    void pointerSets_saveListApplyRemoveRoundTrips()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();
        QVERIFY(sets);

        c.setChain(QVariantList{layerMap(QStringLiteral("comet")), layerMap(QStringLiteral("halo"))});

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("My Look"), QStringLiteral("a test look")));
        QCOMPARE(setsSpy.count(), 1);

        const QVariantMap set = rowFor(sets, QStringLiteral("My Look"));
        QVERIFY(!set.isEmpty());
        QCOMPARE(sets->availableSets().size(), 1);
        QCOMPARE(set.value(QStringLiteral("description")).toString(), QStringLiteral("a test look"));
        QCOMPARE(set.value(QStringLiteral("slug")).toString(), QStringLiteral("my-look"));
        // One chain, so one entry, whatever the layer count.
        QCOMPARE(set.value(QStringLiteral("coverageCount")).toInt(), 1);
        QCOMPARE(set.value(QStringLiteral("coverage")).toStringList(), (QStringList{QStringLiteral("pointer")}));
        QVERIFY2(set.value(QStringLiteral("modified")).toDateTime().isValid(), "the row must carry the file mtime");
        QVERIFY2(set.value(QStringLiteral("active")).toBool(), "a just-saved set must read as active");

        // Any edit clears the badge: apply replaces, so live must equal the set.
        c.addLayer(QStringLiteral("sparks"));
        QVERIFY2(!rowFor(sets, QStringLiteral("My Look")).value(QStringLiteral("active")).toBool(),
                 "editing the live chain away from the set must clear its active flag");

        QVERIFY(sets->applySet(QStringLiteral("My Look")));
        QCOMPARE(chainIds(settings), (QStringList{QStringLiteral("comet"), QStringLiteral("halo")}));
        QVERIFY2(rowFor(sets, QStringLiteral("My Look")).value(QStringLiteral("active")).toBool(),
                 "the set must read as active again right after applying it");

        QSignalSpy removeSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(sets->removeSet(QStringLiteral("My Look")));
        QCOMPARE(removeSpy.count(), 1);
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// Apply REPLACES rather than merging. Decoration and motion sets merge, so
    /// without this a copied-across merge implementation would leave layers the
    /// set never mentioned sitting in the chain and nothing would catch it.
    void applySet_replacesTheWholeChain()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QVariantList{layerMap(QStringLiteral("comet"))});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("comet-only"), QString()));

        // A completely different chain, sharing no layer with the set.
        c.setChain(QVariantList{layerMap(QStringLiteral("halo")), layerMap(QStringLiteral("sparks"))});

        QVERIFY(sets->applySet(QStringLiteral("comet-only")));
        // The load-bearing assertion: nothing of the old chain survived.
        QCOMPARE(chainIds(settings), (QStringList{QStringLiteral("comet")}));
    }

    /// A set captures the layers' parameter overrides, not just their ids.
    void applySet_restoresLayerParameters()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.addLayer(QStringLiteral("comet"));
        c.setLayerParam(0, QStringLiteral("width"), 6);
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Wide"), QString()));

        c.setLayerParam(0, QStringLiteral("width"), 2);
        QVERIFY(sets->applySet(QStringLiteral("Wide")));
        QCOMPARE(settings.pointerChain().layers.size(), 1);
        QCOMPARE(settings.pointerChain().layers.first().parameters.value(QStringLiteral("width")).toInt(), 6);
    }

    /// A set never carries the master switch. Saving with the feature off and
    /// applying with it on (or the reverse) must leave the switch alone: a set
    /// is a look, and whether pointer effects run at all is a separate choice.
    void applySet_leavesTheMasterSwitchAlone()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setEnabled(false);
        c.setChain(QVariantList{layerMap(QStringLiteral("comet"))});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Off Look"), QString()));

        c.setEnabled(true);
        c.setChain(QVariantList{layerMap(QStringLiteral("halo"))});
        QVERIFY(sets->applySet(QStringLiteral("Off Look")));
        QCOMPARE(chainIds(settings), (QStringList{QStringLiteral("comet")}));
        QVERIFY2(c.enabled(), "applying a set must not switch the pointer feature off");
    }

    /// Saving an empty chain is refused: the resulting set would be a no-op
    /// that applySet then rejects, so it must never reach disk.
    void saveSet_emptyChainRejected()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QSignalSpy toastSpy(sets, &ShaderSetStore::toastRequested);
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Nothing"), QString()),
                 "saving an empty pointer chain must be refused");
        QCOMPARE(setsSpy.count(), 0);
        QCOMPARE(toastSpy.count(), 1);
        QCOMPARE(toastSpy.first().first().toString(), PhosphorI18n::tr("There is nothing to capture yet."));
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// The `active` flag is derived from live state, so a live edit made on the
    /// Chain page must re-fire setsChanged, or the badge goes stale on screen.
    void pointerSets_liveEditRefreshesRows()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QVariantList{layerMap(QStringLiteral("comet"))});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Live"), QString()));

        // Drain the notify the setChain above already queued. Without this the
        // spy below would catch THAT emission and pass with the live-state
        // connection severed.
        QSignalSpy spy(sets, &ShaderSetStore::setsChanged);
        QTest::qWait(0);
        QCOMPARE(spy.count(), 1);
        spy.clear();

        c.addLayer(QStringLiteral("halo"));
        QVERIFY2(spy.wait(1000), "a live chain edit must refresh the set rows");
    }

    // ─── updateSet ──────────────────────────────────────────────────────────

    /// updateSet keeps the payload while renaming and editing the description,
    /// and frees the old name. Renaming onto an existing set is refused.
    void pointerSets_updateRoundTripsAndRefusesCollision()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QVariantList{layerMap(QStringLiteral("comet"))});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Old Name"), QStringLiteral("keep me")));
        c.setChain(QVariantList{layerMap(QStringLiteral("halo"))});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Other"), QString()));

        QVERIFY(sets->updateSet(QStringLiteral("Old Name"), QStringLiteral("New Name"), QStringLiteral("new words")));
        QCOMPARE(sets->availableSets().size(), 2);
        QVERIFY2(rowFor(sets, QStringLiteral("Old Name")).isEmpty(), "the old name must be freed");

        const QVariantMap renamed = rowFor(sets, QStringLiteral("New Name"));
        QVERIFY(!renamed.isEmpty());
        QCOMPARE(renamed.value(QStringLiteral("description")).toString(), QStringLiteral("new words"));

        // The payload survived the rename.
        QVERIFY(sets->applySet(QStringLiteral("New Name")));
        QCOMPARE(chainIds(settings), (QStringList{QStringLiteral("comet")}));

        // A same-name call is a description-only edit; an empty description
        // clears the field.
        QVERIFY(sets->updateSet(QStringLiteral("New Name"), QStringLiteral("New Name"), QString()));
        const QVariantMap cleared = rowFor(sets, QStringLiteral("New Name"));
        QVERIFY(!cleared.isEmpty());
        QVERIFY(cleared.value(QStringLiteral("description")).toString().isEmpty());

        QVERIFY2(!sets->updateSet(QStringLiteral("New Name"), QStringLiteral("Other"), QString()),
                 "rename onto an existing set must be refused");
        QCOMPARE(sets->availableSets().size(), 2);
        QVERIFY(!rowFor(sets, QStringLiteral("New Name")).isEmpty());
        QVERIFY(!rowFor(sets, QStringLiteral("Other")).isEmpty());
    }

    /// Saving over an existing name destroys the stored payload and no Discard
    /// brings a set file back, so it needs explicit consent.
    void saveSet_overwriteNeedsConsent()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QVariantList{layerMap(QStringLiteral("comet"))});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Taken"), QString()));

        c.setChain(QVariantList{layerMap(QStringLiteral("halo"))});
        QCOMPARE(sets->existingSetName(QStringLiteral("taken")), QStringLiteral("Taken"));
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Taken"), QString()),
                 "an unconfirmed overwrite must be refused");
        QVERIFY(sets->applySet(QStringLiteral("Taken")));
        QCOMPARE(chainIds(settings), (QStringList{QStringLiteral("comet")}));

        c.setChain(QVariantList{layerMap(QStringLiteral("halo"))});
        QVERIFY2(sets->saveCurrentAsSet(QStringLiteral("Taken"), QString(), /*overwrite=*/true),
                 "a confirmed overwrite must be honoured");
        QCOMPARE(sets->availableSets().size(), 1);
        c.setChain(QVariantList{layerMap(QStringLiteral("comet"))});
        QVERIFY(sets->applySet(QStringLiteral("Taken")));
        QCOMPARE(chainIds(settings), (QStringList{QStringLiteral("halo")}));
    }

    // ─── Export / import ────────────────────────────────────────────────────

    /// Export writes a file that import reads back, in both the local-path and
    /// the file:// URL form. Importing while the original is present must land
    /// under a free name rather than overwriting it.
    void pointerSets_exportImportRoundTrips()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QVariantList{layerMap(QStringLiteral("comet"))});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Portable"), QString()));

        QTemporaryDir exportDir;
        QVERIFY(exportDir.isValid());
        const QString exported = exportDir.filePath(QStringLiteral("portable.json"));
        QVERIFY(sets->exportSet(QStringLiteral("Portable"), exported));
        QVERIFY(QFile::exists(exported));

        QVERIFY(sets->importSet(exported));
        QCOMPARE(sets->availableSets().size(), 2);
        QVERIFY2(!rowFor(sets, QStringLiteral("Portable (2)")).isEmpty(),
                 "a colliding import must land under a free name");

        QVERIFY2(sets->importSet(QUrl::fromLocalFile(exported).toString()),
                 "importSet must accept the file:// URL form the drop zone emits");
        QCOMPARE(sets->availableSets().size(), 3);

        const QVariantList all = sets->availableSets();
        for (const QVariant& row : all)
            QVERIFY(sets->removeSet(row.toMap().value(QStringLiteral("name")).toString()));
        QVERIFY(sets->availableSets().isEmpty());

        QVERIFY(sets->importSet(exported));
        QCOMPARE(sets->availableSets().size(), 1);
        QVERIFY(!rowFor(sets, QStringLiteral("Portable")).isEmpty());

        c.setChain(QVariantList{layerMap(QStringLiteral("halo"))});
        QVERIFY(sets->applySet(QStringLiteral("Portable")));
        QCOMPARE(chainIds(settings), (QStringList{QStringLiteral("comet")}));
    }

    // ─── Format-version gate ────────────────────────────────────────────────

    /// A set written by a NEWER build may carry fields this one drops on parse,
    /// so it is refused on both apply and import rather than committing a
    /// silently truncated look.
    void pointerSets_newerFormatVersionRefused()
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QVariantList{layerMap(QStringLiteral("halo"))});

        const QJsonObject root = setRoot(QStringLiteral("From the Future"),
                                         QJsonArray{entryWith(QStringLiteral("pointer"), validProfileJson())},
                                         /*version=*/2);
        writePointerSetFile(pointerSetsDir() + QStringLiteral("/from-the-future.json"), root);

        QVERIFY2(!sets->applySet(QStringLiteral("From the Future")), "a newer-version set must not apply");
        QCOMPARE(chainIds(settings), (QStringList{QStringLiteral("halo")}));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString external = dir.filePath(QStringLiteral("future.json"));
        writePointerSetFile(external, root);
        QVERIFY2(!sets->importSet(external), "a newer-version set must not import");
    }

    // ─── Validator refusals, one slot each ──────────────────────────────────

    /// The pointer domain has no baseline concept, so one arriving through an
    /// import could never be seen, applied or cleared. Refuse it at the
    /// boundary, as the other two domains do.
    void applySet_rejectsASetCarryingABaseline()
    {
        QJsonObject root =
            setRoot(QStringLiteral("Based"), QJsonArray{entryWith(QStringLiteral("pointer"), validProfileJson())});
        root.insert(QStringLiteral("baseline"), validProfileJson());
        assertRefused(QStringLiteral("based"), root);
    }

    /// A present-but-non-array `overrides` would otherwise read as "no
    /// overrides" and let the file apply with its payload silently dropped.
    void applySet_rejectsNonArrayOverrides()
    {
        assertRefused(QStringLiteral("not-an-array"),
                      setRoot(QStringLiteral("Not An Array"), QJsonValue(QStringLiteral("pointer"))));
    }

    /// Zero entries covers nothing.
    void applySet_rejectsZeroEntries()
    {
        assertRefused(QStringLiteral("empty"), setRoot(QStringLiteral("Empty"), QJsonArray{}));
    }

    /// Two entries contradict each other about what the one chain is.
    void applySet_rejectsTwoEntries()
    {
        const QJsonObject entry = entryWith(QStringLiteral("pointer"), validProfileJson());
        assertRefused(QStringLiteral("doubled"), setRoot(QStringLiteral("Doubled"), QJsonArray{entry, entry}));
    }

    /// An entry that is not an object at all.
    void applySet_rejectsNonObjectEntry()
    {
        assertRefused(QStringLiteral("scalar"),
                      setRoot(QStringLiteral("Scalar"), QJsonArray{QJsonValue(QStringLiteral("pointer"))}));
    }

    /// "pointer" is the only path a pointer set may carry. Anything else is a
    /// file from another domain, or an attempt at traversal.
    void applySet_rejectsWrongPath()
    {
        assertRefused(QStringLiteral("foreign"),
                      setRoot(QStringLiteral("Foreign"),
                              QJsonArray{entryWith(QStringLiteral("window.tiled"), validProfileJson())}));
    }

    /// A profile that is not an object.
    void applySet_rejectsNonObjectProfile()
    {
        assertRefused(QStringLiteral("stringy"),
                      setRoot(QStringLiteral("Stringy"),
                              QJsonArray{entryWith(QStringLiteral("pointer"), QJsonValue(QStringLiteral("comet")))}));
    }

    /// fromJson drops unknown and wrong-typed keys, so `{"layers": "comet"}` is
    /// a non-empty object that parses to an EMPTY chain. Staging that would
    /// make the set read as covering the pointer while changing nothing, so the
    /// validator judges the parsed profile rather than the raw object.
    void applySet_rejectsAProfileThatParsesToAnEmptyChain()
    {
        QJsonObject hollow;
        hollow.insert(QStringLiteral("layers"), QStringLiteral("comet"));
        assertRefused(QStringLiteral("hollow"),
                      setRoot(QStringLiteral("Hollow"), QJsonArray{entryWith(QStringLiteral("pointer"), hollow)}));
    }

private:
    /// Write @p root as a set file under @p slug, then assert applySet refuses
    /// it and leaves the live chain untouched. Shared by every refusal slot so
    /// each one is the malformed payload and nothing else.
    void assertRefused(const QString& slug, const QJsonObject& root)
    {
        StubSettings settings;
        PointerPageController c(nullptr, &settings);
        c.setSetsDirOverride(pointerSetsDir());
        ShaderSetStore* sets = c.setsBridge();

        // A live chain that must survive the refusal.
        c.setChain(QVariantList{layerMap(QStringLiteral("halo"))});
        writePointerSetFile(pointerSetsDir() + QLatin1Char('/') + slug + QStringLiteral(".json"), root);

        QVERIFY2(!sets->applySet(root.value(QStringLiteral("name")).toString()),
                 "a malformed pointer set must be refused");
        QCOMPARE(chainIds(settings), (QStringList{QStringLiteral("halo")}));
    }
};

QTEST_MAIN(TestPointerSets)

#include "test_pointer_sets.moc"
