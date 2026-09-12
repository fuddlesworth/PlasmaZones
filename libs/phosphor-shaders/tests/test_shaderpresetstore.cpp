// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// The preset STORE half: the directory loader and its live-reload watcher, the
// one-shot legacy overlay-preset migration, and ShaderPresetStore itself (load
// idempotence, family selection, root refusal, teardown order).
//
// Split from test_shaderpresets.cpp when that file reached the size ceiling. The PARSE
// and RESOLVE halves stay there: this file is about getting presets off disk and into a
// registry, that one is about what a preset MEANS once it is there.
//
// The fixture helpers are deliberately the same shape as the sibling file rather than
// shared through a header: two small test binaries each carrying writeFile/jsonFrom is
// cheaper than a third translation unit, and the sibling states the same reasoning.

#include <PhosphorShaders/ShaderPreset.h>
#include <PhosphorShaders/ShaderPresetParse.h>
#include <PhosphorShaders/ShaderPresetRegistry.h>
#include <PhosphorShaders/ShaderPresetStore.h>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace PhosphorShaders;

namespace {
Q_LOGGING_CATEGORY(lcTest, "phosphorshaders.test.presets")

QJsonObject jsonFrom(const QString& text)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    Q_ASSERT(err.error == QJsonParseError::NoError);
    return doc.object();
}

bool writeFile(const QString& path, const QString& body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    return file.write(body.toUtf8()) == body.toUtf8().size();
}

/// A store publishing exactly @p family out of @p root.
///
/// Every scan-side test drives the STORE, because the publisher is no longer a
/// class of its own: the store owns the parse sink and the directory loader
/// directly, which is what gives the "one publisher per family" invariant an
/// owner instead of leaving it as an assumption three classes each half-held.
///
/// These tests lose nothing by the change. Every one of them already wrote into
/// `userPresetDirectory(root, family)` — the store's own layout — so pointing a
/// loader at an arbitrary directory was a capability none of them used. They
/// gain the real watcher settings the product runs with, since `load()` always
/// registers LiveReload::On.
///
/// Returned by pointer because the store is a QObject: non-copyable, and the
/// caller needs it to outlive the assertions that read its registry.
std::unique_ptr<ShaderPresetStore> storePublishing(const QString& root, ShaderFamily family)
{
    auto store = std::make_unique<ShaderPresetStore>();
    store->load(root, {family});
    return store;
}
} // namespace

class TestShaderPresetStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ─────── loader ───────

    void loadsPresetFilesFromDisk();
    void filenameStemIsTheFallbackId();
    void skipsPresetWithNoPackId();
    void editingAPresetFileReachesTheRegistry();
    void watcherSeesAPlainRewrite();
    void watcherSeesAnAtomicRenameSave();
    void watcherSeesAPresetAddedToAFreshInstall();

    // ─────── legacy migration ───────

    void migratesLegacyOverlayPreset();
    void migrationIsIdempotent();
    void migratedIdIsDerivedNotRandom();
    void migrationLeavesForeignFilesAlone();
    void migrationFallsBackToFilenameForName();
    void migrationOnAbsentRootIsNoOp();

    // ─────── store ───────

    void storeDestructionDoesNotTouchAFreedRegistry();
    void storeLoadIsIdempotent();
    void storeRefusesANonAbsoluteRoot();
    void anIdThatIsNotAPathComponentIsRefused();
    void packPresetsAreRetractedForAVanishedPack();
    void resolveParamsClampsToTheDeclaredRange();
    void storeLoadsOnlyTheFamiliesTheConsumerNames();
};

// ═══════════════════════ loader ═══════════════════════

void TestShaderPresetStore::loadsPresetFilesFromDisk()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = userPresetDirectory(root.path(), ShaderFamily::Animation);
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(writeFile(dir + QStringLiteral("/neon.json"), QStringLiteral(R"({
        "id": "{neon}", "name": "Neon Pulse", "packId": "dissolve",
        "params": { "speed": 1.4 }
    })")));

    const auto store = storePublishing(root.path(), ShaderFamily::Animation);

    const QList<ShaderPreset> presets =
        store->registry().presetsFor(ShaderFamily::Animation, QStringLiteral("dissolve"));
    QCOMPARE(presets.size(), 1);
    QCOMPARE(presets.at(0).id, QStringLiteral("{neon}"));
    QCOMPARE(presets.at(0).name, QStringLiteral("Neon Pulse"));
    QVERIFY(!presets.at(0).readOnly);
    QCOMPARE(presets.at(0).params.value(QStringLiteral("speed")).toDouble(), 1.4);
}

void TestShaderPresetStore::filenameStemIsTheFallbackId()
{
    // A hand-written file with no `id` still gets a stable identity an
    // assignment can point at, rather than being skipped.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = userPresetDirectory(root.path(), ShaderFamily::Pointer);
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(writeFile(dir + QStringLiteral("/my-preset.json"),
                      QStringLiteral(R"({ "name": "Mine", "packId": "trail", "params": {} })")));

    const auto store = storePublishing(root.path(), ShaderFamily::Pointer);

    const QList<ShaderPreset> presets = store->registry().presetsFor(ShaderFamily::Pointer, QStringLiteral("trail"));
    QCOMPARE(presets.size(), 1);
    QCOMPARE(presets.at(0).id, QStringLiteral("my-preset"));
}

void TestShaderPresetStore::skipsPresetWithNoPackId()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = userPresetDirectory(root.path(), ShaderFamily::Overlay);
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(writeFile(dir + QStringLiteral("/orphan.json"),
                      QStringLiteral(R"({ "name": "Orphan", "params": { "x": 1 } })")));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("preset file with no pack id")));
    const auto store = storePublishing(root.path(), ShaderFamily::Overlay);

    // A preset naming no pack cannot be offered anywhere: parameter ids mean
    // nothing across packs.
    QVERIFY(store->registry().presetsFor(ShaderFamily::Overlay, QStringLiteral("aurora")).isEmpty());
}

void TestShaderPresetStore::editingAPresetFileReachesTheRegistry()
{
    // THE point of the feature: an assignment holds a reference, so a preset
    // retuned on disk has to reach the registry every consumer resolves
    // through. Driven with an explicit rescan rather than the watcher, so the
    // test asserts the commit path without depending on filesystem-event
    // timing; the watcher itself is DirectoryLoader's own tested behaviour.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = userPresetDirectory(root.path(), ShaderFamily::Animation);
    QVERIFY(QDir().mkpath(dir));
    const QString file = dir + QStringLiteral("/neon.json");
    QVERIFY(writeFile(file, QStringLiteral(R"({
        "id": "{neon}", "name": "Neon", "packId": "dissolve", "params": { "speed": 1.0 }
    })")));

    const auto store = storePublishing(root.path(), ShaderFamily::Animation);
    ShaderPresetRegistry& registry = store->registry();

    const QVariantMap deltas{{QStringLiteral("glow"), 0.5}};
    QVariantMap resolved =
        registry.resolveParams(ShaderFamily::Animation, QStringLiteral("dissolve"), QStringLiteral("{neon}"), deltas);
    QCOMPARE(resolved.value(QStringLiteral("speed")).toDouble(), 1.0);
    QCOMPARE(resolved.value(QStringLiteral("glow")).toDouble(), 0.5);

    QSignalSpy spy(&registry, &ShaderPresetRegistry::presetsChanged);
    QVERIFY(writeFile(file, QStringLiteral(R"({
        "id": "{neon}", "name": "Neon", "packId": "dissolve", "params": { "speed": 9.0 }
    })")));
    // The store's own synchronous rescan, which is the entry point the write
    // side uses after saving a preset. It answers false for a family this store
    // does not publish, so assert it actually ran.
    QVERIFY(store->rescanNow(ShaderFamily::Animation));

    QCOMPARE(spy.count(), 1);
    resolved =
        registry.resolveParams(ShaderFamily::Animation, QStringLiteral("dissolve"), QStringLiteral("{neon}"), deltas);
    // The retuned value flows through...
    QCOMPARE(resolved.value(QStringLiteral("speed")).toDouble(), 9.0);
    // ...and the assignment's own edit survives the preset change.
    QCOMPARE(resolved.value(QStringLiteral("glow")).toDouble(), 0.5);
}

// ═══════════════════════ legacy migration ═══════════════════════

void TestShaderPresetStore::migratesLegacyOverlayPreset()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    // The exact shape the old zone-only save dialog wrote: flat in the root,
    // `shaderId` / `shaderParams`, identity carried by the filename.
    QVERIFY(writeFile(root.path() + QStringLiteral("/my-aurora.json"), QStringLiteral(R"({
        "name": "My Aurora", "shaderId": "aurora",
        "shaderParams": { "speed": 0.7, "tint": "#ff8800" }
    })")));

    QCOMPARE(migrateLegacyOverlayPresets(root.path()), 1);

    // The original is gone from the root...
    QVERIFY(!QFile::exists(root.path() + QStringLiteral("/my-aurora.json")));

    // ...and the preset now loads through the ordinary overlay path.
    const auto store = storePublishing(root.path(), ShaderFamily::Overlay);

    const QList<ShaderPreset> presets = store->registry().presetsFor(ShaderFamily::Overlay, QStringLiteral("aurora"));
    QCOMPARE(presets.size(), 1);
    QCOMPARE(presets.at(0).name, QStringLiteral("My Aurora"));
    QCOMPARE(presets.at(0).packId, QStringLiteral("aurora"));
    QCOMPARE(presets.at(0).params.value(QStringLiteral("speed")).toDouble(), 0.7);
    QCOMPARE(presets.at(0).params.value(QStringLiteral("tint")).toString(), QStringLiteral("#ff8800"));
    QVERIFY(!presets.at(0).readOnly);
}

void TestShaderPresetStore::migrationIsIdempotent()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.path() + QStringLiteral("/one.json"),
                      QStringLiteral(R"({ "name": "One", "shaderId": "aurora", "shaderParams": {} })")));

    QCOMPARE(migrateLegacyOverlayPresets(root.path()), 1);
    // Nothing left in the root, so a second run has nothing to do. Running on
    // every startup must not keep manufacturing copies.
    QCOMPARE(migrateLegacyOverlayPresets(root.path()), 0);

    const QDir overlayDir(userPresetDirectory(root.path(), ShaderFamily::Overlay));
    QCOMPARE(overlayDir.entryList({QStringLiteral("*.json")}, QDir::Files).size(), 1);
}

void TestShaderPresetStore::migratedIdIsDerivedNotRandom()
{
    // Two processes racing the migration have to agree on the target path, or
    // the user ends up with the preset twice. Same input filename, same id.
    const auto idFor = [](const QString& rootPath) {
        const QDir dir(userPresetDirectory(rootPath, ShaderFamily::Overlay));
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
        return files.isEmpty() ? QString() : files.first();
    };

    QTemporaryDir rootA;
    QTemporaryDir rootB;
    QVERIFY(rootA.isValid() && rootB.isValid());
    const QString body = QStringLiteral(R"({ "name": "X", "shaderId": "aurora", "shaderParams": {} })");
    QVERIFY(writeFile(rootA.path() + QStringLiteral("/same-name.json"), body));
    QVERIFY(writeFile(rootB.path() + QStringLiteral("/same-name.json"), body));

    QCOMPARE(migrateLegacyOverlayPresets(rootA.path()), 1);
    QCOMPARE(migrateLegacyOverlayPresets(rootB.path()), 1);

    QVERIFY(!idFor(rootA.path()).isEmpty());
    QCOMPARE(idFor(rootA.path()), idFor(rootB.path()));
}

void TestShaderPresetStore::migrationLeavesForeignFilesAlone()
{
    // A file in the same directory that this never wrote is not ours to move or
    // delete — the migration owns the shape it produced, not the directory.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString foreign = root.path() + QStringLiteral("/notes.json");
    QVERIFY(writeFile(foreign, QStringLiteral(R"({ "something": "else" })")));

    QCOMPARE(migrateLegacyOverlayPresets(root.path()), 0);
    QVERIFY(QFile::exists(foreign));
}

void TestShaderPresetStore::migrationFallsBackToFilenameForName()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(writeFile(root.path() + QStringLiteral("/deep-teal.json"),
                      QStringLiteral(R"({ "shaderId": "aurora", "shaderParams": {} })")));

    QCOMPARE(migrateLegacyOverlayPresets(root.path()), 1);

    const auto store = storePublishing(root.path(), ShaderFamily::Overlay);
    // A nameless preset would render as a blank picker row; the old filename is
    // the best name available and is what the user recognises.
    QCOMPARE(store->registry().presetsFor(ShaderFamily::Overlay, QStringLiteral("aurora")).at(0).name,
             QStringLiteral("deep-teal"));
}

void TestShaderPresetStore::migrationOnAbsentRootIsNoOp()
{
    QCOMPARE(migrateLegacyOverlayPresets(QStringLiteral("/nonexistent/plasmazones/shader-presets")), 0);
}

void TestShaderPresetStore::watcherSeesAPlainRewrite()
{
    // The live-reload path, driven by the real QFileSystemWatcher rather than
    // by an explicit rescan. This is what a text editor save has to trigger,
    // and it is the whole premise of assignment-by-reference.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = userPresetDirectory(root.path(), ShaderFamily::Animation);
    QVERIFY(QDir().mkpath(dir));
    const QString file = dir + QStringLiteral("/neon.json");
    QVERIFY(writeFile(file, QStringLiteral(R"({
        "id": "{neon}", "name": "Neon", "packId": "dissolve", "params": { "speed": 1.0 }
    })")));

    const auto store = storePublishing(root.path(), ShaderFamily::Animation);
    ShaderPresetRegistry& registry = store->registry();
    QCOMPARE(registry.preset(ShaderFamily::Animation, QStringLiteral("dissolve"), QStringLiteral("{neon}"))
                 .params.value(QStringLiteral("speed"))
                 .toDouble(),
             1.0);

    QSignalSpy spy(&registry, &ShaderPresetRegistry::presetsChanged);
    QVERIFY(writeFile(file, QStringLiteral(R"({
        "id": "{neon}", "name": "Neon", "packId": "dissolve", "params": { "speed": 9.0 }
    })")));

    QVERIFY2(spy.wait(5000), "the watcher never reported the rewritten preset");
    QCOMPARE(registry.preset(ShaderFamily::Animation, QStringLiteral("dissolve"), QStringLiteral("{neon}"))
                 .params.value(QStringLiteral("speed"))
                 .toDouble(),
             9.0);
}

void TestShaderPresetStore::watcherSeesAnAtomicRenameSave()
{
    // How most editors actually save: write a sibling temp file, then rename it
    // over the original. The inode changes, so a watch on the FILE would be
    // left pointing at a file nobody has any more — only a watch on the
    // directory survives it.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = userPresetDirectory(root.path(), ShaderFamily::Animation);
    QVERIFY(QDir().mkpath(dir));
    const QString file = dir + QStringLiteral("/neon.json");
    QVERIFY(writeFile(file, QStringLiteral(R"({
        "id": "{neon}", "name": "Neon", "packId": "dissolve", "params": { "speed": 1.0 }
    })")));

    const auto store = storePublishing(root.path(), ShaderFamily::Animation);
    ShaderPresetRegistry& registry = store->registry();

    QSignalSpy spy(&registry, &ShaderPresetRegistry::presetsChanged);

    const QString tmpFile = dir + QStringLiteral("/neon.json.tmp");
    QVERIFY(writeFile(tmpFile, QStringLiteral(R"({
        "id": "{neon}", "name": "Neon", "packId": "dissolve", "params": { "speed": 7.0 }
    })")));
    QVERIFY(QFile::remove(file));
    QVERIFY(QFile::rename(tmpFile, file));

    QVERIFY2(spy.wait(5000), "the watcher never reported the atomically-saved preset");
    QCOMPARE(registry.preset(ShaderFamily::Animation, QStringLiteral("dissolve"), QStringLiteral("{neon}"))
                 .params.value(QStringLiteral("speed"))
                 .toDouble(),
             7.0);
}

void TestShaderPresetStore::watcherSeesAPresetAddedToAFreshInstall()
{
    // The directory does not exist when the loader starts, which is every
    // process on a machine that has never saved a preset. The daemon and the
    // compositor both register their watch at startup, long before the settings
    // app creates the directory, so a watch that could not survive that would
    // leave them blind until the next restart.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = userPresetDirectory(root.path(), ShaderFamily::Animation);
    QVERIFY(!QDir(dir).exists());

    const auto store = storePublishing(root.path(), ShaderFamily::Animation);
    ShaderPresetRegistry& registry = store->registry();

    QSignalSpy spy(&registry, &ShaderPresetRegistry::presetsChanged);
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(writeFile(dir + QStringLiteral("/neon.json"), QStringLiteral(R"({
        "id": "{neon}", "name": "Neon", "packId": "dissolve", "params": { "speed": 3.0 }
    })")));

    QVERIFY2(spy.wait(5000), "the watcher never reported the first preset in a fresh directory");
    QCOMPARE(registry.preset(ShaderFamily::Animation, QStringLiteral("dissolve"), QStringLiteral("{neon}"))
                 .params.value(QStringLiteral("speed"))
                 .toDouble(),
             3.0);
}

// ─────── store ───────

void TestShaderPresetStore::storeDestructionDoesNotTouchAFreedRegistry()
{
    // No test constructed a ShaderPresetStore at all, which is why a
    // use-after-free on its teardown was invisible to a green suite: the store
    // parented both the registry and its loaders to itself, and QObject frees
    // children in insertion order, so the registry died first while each
    // loader's destructor was still retracting through it.
    //
    // The fault is now STRUCTURALLY unreachable rather than ordered-around. The
    // registry is a by-value member declared before the publisher slots, so it
    // outlives them under ordinary member-destruction rules, and the destructor
    // retracts NOTHING — it only resets each publisher, loader before sink. There is
    // no QObject child destruction in the path at all, and nothing for a QPointer to
    // catch, which is why there is no longer a QPointer.
    //
    // What this test can still only do weakly is PROVE the absence: freed-memory
    // reuse is nondeterministic and this repo has no sanitizer configuration.
    // Run it under ASAN for the stronger answer. What it does assert is that the
    // teardown runs to completion twice over and that the registry is readable
    // right up to the end of each scope.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("animation"))));
    QVERIFY(writeFile(dir.filePath(QStringLiteral("animation/a.json")), QStringLiteral(R"({
        "id": "a", "name": "A", "packId": "dissolve", "params": { "speed": 2.0 }
    })")));

    {
        ShaderPresetStore store;
        store.load(dir.path());
        QCOMPARE(store.registry().presetsFor(ShaderFamily::Animation, QStringLiteral("dissolve")).size(), 1);
    }
    // Reaching here without faulting is the assertion. A second construct-and-
    // destroy pass catches a teardown that corrupted process-wide state.
    {
        ShaderPresetStore store;
        store.load(dir.path());
        QCOMPARE(store.registry().presetsFor(ShaderFamily::Animation, QStringLiteral("dissolve")).size(), 1);
    }
}

void TestShaderPresetStore::storeLoadIsIdempotent()
{
    // A second load() used to build four more loaders, leak the first four with their
    // watchers armed, and leave two publishers per family. The slot IS the publisher
    // now, so there is nowhere for a second one to go.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("animation"))));
    QVERIFY(writeFile(dir.filePath(QStringLiteral("animation/a.json")), QStringLiteral(R"({
        "id": "a", "name": "A", "packId": "dissolve", "params": { "speed": 2.0 }
    })")));

    ShaderPresetStore store;
    store.load(dir.path());
    QVERIFY(store.publishes(ShaderFamily::Animation));

    store.load(dir.path());
    // One publisher still, and the presets are not doubled. Now guaranteed by
    // the slot rather than by an early return: there is one slot per family, so
    // a second publisher is not a thing the store can be asked to build.
    QVERIFY(store.publishes(ShaderFamily::Animation));
    QCOMPARE(store.registry().presetsFor(ShaderFamily::Animation, QStringLiteral("dissolve")).size(), 1);
}

void TestShaderPresetStore::storeRefusesANonAbsoluteRoot()
{
    // An empty root resolves to the process working directory: QDir("") is
    // QDir("."), whose exists() is true, so the migration would scan and read
    // the CWD's *.json files and userPresetDirectory would hand out
    // filesystem-root paths. A relative root is the same hazard.
    ShaderPresetStore store;
    store.load(QString());
    QVERIFY(!store.publishes(ShaderFamily::Animation));
    // And the write-side entry point reports the refusal rather than silently
    // doing nothing, which is what a caller that saved a file needs to hear.
    QVERIFY(!store.rescanNow(ShaderFamily::Animation));

    ShaderPresetStore relative;
    relative.load(QStringLiteral("shader-presets"));
    QVERIFY(!relative.publishes(ShaderFamily::Animation));
}

void TestShaderPresetStore::anIdThatIsNotAPathComponentIsRefused()
{
    // The id becomes a filename on the write side, so one carrying a separator
    // or a parent hop escapes the preset directory the moment the user renames
    // that preset.
    QVERIFY(!ShaderPreset::isUsableId(QString()));
    QVERIFY(!ShaderPreset::isUsableId(QStringLiteral(".")));
    QVERIFY(!ShaderPreset::isUsableId(QStringLiteral("..")));
    QVERIFY(!ShaderPreset::isUsableId(QStringLiteral("../../etc/passwd")));
    QVERIFY(!ShaderPreset::isUsableId(QStringLiteral("a/b")));
    QVERIFY(!ShaderPreset::isUsableId(QStringLiteral("a\\b")));
    QVERIFY(ShaderPreset::isUsableId(QStringLiteral("6f2b8d51-4c3a-4e7f-9b10-2d8e4a5c7b63")));

    // And the parse path falls back to the filename stem rather than carrying
    // the escaping id through.
    const ShaderPreset parsed = ShaderPreset::fromJson(jsonFrom(QStringLiteral(R"({
        "id": "../../../../pwn", "name": "Bad", "packId": "dissolve"
    })")),
                                                       QStringLiteral("safe-stem"));
    QCOMPARE(parsed.id, QStringLiteral("safe-stem"));
}

void TestShaderPresetStore::packPresetsAreRetractedForAVanishedPack()
{
    // A per-pack upsert driven by the packs that currently exist can never name
    // a pack that has GONE, so an uninstalled pack's presets used to survive for
    // the life of the process and keep being offered.
    ShaderPresetRegistry registry;
    QHash<QString, PackPresets> byPack;
    byPack.insert(QStringLiteral("dissolve"), PackPresets{{QStringLiteral("Soft"), {{QStringLiteral("speed"), 1.0}}}});
    byPack.insert(QStringLiteral("aurora"), PackPresets{{QStringLiteral("Bright"), {{QStringLiteral("glow"), 2.0}}}});
    registry.setPackPresetsForFamily(ShaderFamily::Animation, byPack);
    QCOMPARE(registry.presetsFor(ShaderFamily::Animation, QStringLiteral("aurora")).size(), 1);

    QSignalSpy spy(&registry, &ShaderPresetRegistry::presetsChanged);
    // `aurora` is uninstalled: it simply is not in the new set.
    byPack.remove(QStringLiteral("aurora"));
    registry.setPackPresetsForFamily(ShaderFamily::Animation, byPack);

    QVERIFY(registry.presetsFor(ShaderFamily::Animation, QStringLiteral("aurora")).isEmpty());
    // And the pack that did not change must not be re-signalled.
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("aurora"));
    QCOMPARE(registry.presetsFor(ShaderFamily::Animation, QStringLiteral("dissolve")).size(), 1);
}

void TestShaderPresetStore::storeLoadsOnlyTheFamiliesTheConsumerNames()
{
    // A loader is not one startup scan: it holds a QFileSystemWatcher and
    // re-parses its whole directory every time the user saves a preset there.
    // The compositor paid that on its own thread for the overlay family it
    // cannot consult at all, so a consumer can now name what it resolves.
    QTemporaryDir root;
    QVERIFY(root.isValid());

    // A preset file in each of two families, so "no loader" is distinguishable
    // from "nothing on disk".
    for (const ShaderFamily family : {ShaderFamily::Animation, ShaderFamily::Overlay}) {
        const QString dir = userPresetDirectory(root.path(), family);
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(writeFile(dir + QStringLiteral("/p.json"), QStringLiteral(R"({
            "id": "p", "name": "P", "packId": "pack", "params": { "speed": 1.0 }
        })")));
    }

    ShaderPresetStore store;
    store.load(root.path(), {ShaderFamily::Animation, ShaderFamily::Surface});

    QVERIFY(store.publishes(ShaderFamily::Animation));
    QVERIFY(store.publishes(ShaderFamily::Surface));
    QVERIFY(!store.publishes(ShaderFamily::Pointer));
    QVERIFY(!store.publishes(ShaderFamily::Overlay));

    // The named family's preset is loaded; the unnamed one's is simply absent,
    // which is the registry's documented miss case rather than an error.
    QCOMPARE(store.registry().presetsFor(ShaderFamily::Animation, QStringLiteral("pack")).size(), 1);
    QVERIFY(store.registry().presetsFor(ShaderFamily::Overlay, QStringLiteral("pack")).isEmpty());

    // The default is still every family, so an existing caller is unchanged.
    ShaderPresetStore all;
    all.load(root.path());
    QVERIFY(all.publishes(ShaderFamily::Overlay));
    QCOMPARE(all.registry().presetsFor(ShaderFamily::Overlay, QStringLiteral("pack")).size(), 1);
}

void TestShaderPresetStore::resolveParamsClampsToTheDeclaredRange()
{
    // A pack's declared min/max was enforced only by the settings slider, so a
    // hand-written preset file could drive a parameter anywhere — and several
    // bundled overlay packs feed one straight into a GLSL loop bound.
    ShaderPresetRegistry registry;
    PresetValueBounds bounds;
    bounds.insert(QStringLiteral("octaves"), PresetValueRange(2, 8));
    QHash<QString, PackPresets> byPack;
    byPack.insert(QStringLiteral("cosmic"), PackPresets{{QStringLiteral("Wild"), {{QStringLiteral("octaves"), 9999}}}});
    QHash<QString, PresetValueBounds> boundsByPack;
    boundsByPack.insert(QStringLiteral("cosmic"), bounds);
    registry.setPackPresetsForFamily(ShaderFamily::Overlay, byPack, boundsByPack);

    // The preset's own out-of-range value is clamped...
    QCOMPARE(registry.resolveParams(ShaderFamily::Overlay, QStringLiteral("cosmic"), QStringLiteral("Wild"), {})
                 .value(QStringLiteral("octaves"))
                 .toInt(),
             8);
    // ...and so is a delta, which never passes through a preset at all.
    const QVariantMap deltas{{QStringLiteral("octaves"), 100000}};
    QCOMPARE(registry.resolveParams(ShaderFamily::Overlay, QStringLiteral("cosmic"), QStringLiteral("Wild"), deltas)
                 .value(QStringLiteral("octaves"))
                 .toInt(),
             8);
    // Below the floor too.
    const QVariantMap low{{QStringLiteral("octaves"), -5}};
    QCOMPARE(registry.resolveParams(ShaderFamily::Overlay, QStringLiteral("cosmic"), QStringLiteral("Wild"), low)
                 .value(QStringLiteral("octaves"))
                 .toInt(),
             2);
    // An integral parameter stays integral rather than coming back as a double.
    QCOMPARE(registry.resolveParams(ShaderFamily::Overlay, QStringLiteral("cosmic"), QStringLiteral("Wild"), low)
                 .value(QStringLiteral("octaves"))
                 .typeId(),
             QMetaType::LongLong);

    // The NO-PRESET branch clamps too, and it is the one the three flatteners reach
    // when an assignment names no preset — every assertion above passes an existing
    // preset id, so that branch was never entered. An unknown id takes the same path
    // as an empty one: the deltas are the whole answer, bounded.
    const QVariantMap wild{{QStringLiteral("octaves"), 9999}};
    QCOMPARE(registry.resolveParams(ShaderFamily::Overlay, QStringLiteral("cosmic"), QString(), wild)
                 .value(QStringLiteral("octaves"))
                 .toInt(),
             8);
    QCOMPARE(registry.resolveParams(ShaderFamily::Overlay, QStringLiteral("cosmic"), QStringLiteral("GoneAway"), wild)
                 .value(QStringLiteral("octaves"))
                 .toInt(),
             8);

    // A FRACTIONAL range, which every assertion above misses and which is where the
    // clamp used to fail outright. Qt's JSON reader returns qlonglong for every
    // whole number, `2.0` included, so a whole-number value on a float parameter
    // arrives as an INTEGRAL variant. Preserving that integrality by rounding to
    // nearest put the clamped value straight back out of range: 2 clamped to 0.6 and
    // then rounded to 1, above the declared maximum. No integer fits in this range
    // at all, so the bound wins and the value comes back as the clamped double.
    PresetValueBounds fine;
    fine.insert(QStringLiteral("smoothness"), PresetValueRange(0.01, 0.6));
    QHash<QString, PresetValueBounds> fineByPack;
    fineByPack.insert(QStringLiteral("circle"), fine);
    registry.setPackPresetsForFamily(ShaderFamily::Animation, {}, fineByPack);

    const QVariantMap whole{{QStringLiteral("smoothness"), 2}};
    const QVariant clamped = registry.resolveParams(ShaderFamily::Animation, QStringLiteral("circle"), QString(), whole)
                                 .value(QStringLiteral("smoothness"));
    QCOMPARE(clamped.toDouble(), 0.6);
    // Below the floor, where rounding to nearest would have produced 0.
    const QVariantMap zero{{QStringLiteral("smoothness"), 0}};
    QCOMPARE(registry.resolveParams(ShaderFamily::Animation, QStringLiteral("circle"), QString(), zero)
                 .value(QStringLiteral("smoothness"))
                 .toDouble(),
             0.01);
    // An integral value already INSIDE a range wide enough to hold one keeps its
    // type, so the fix above does not turn every int into a double.
    QCOMPARE(registry
                 .resolveParams(ShaderFamily::Overlay, QStringLiteral("cosmic"), QString(),
                                QVariantMap{{QStringLiteral("octaves"), 5}})
                 .value(QStringLiteral("octaves"))
                 .typeId(),
             QMetaType::LongLong);

    // An INVERTED declared range is refused rather than applied. Nothing validates
    // min <= max, and applying both bounds in order to a backwards pair left the
    // value below the minimum, which is worse than not clamping.
    PresetValueBounds backwards;
    backwards.insert(QStringLiteral("span"), PresetValueRange(5, 1));
    QHash<QString, PresetValueBounds> backwardsByPack;
    backwardsByPack.insert(QStringLiteral("inverted"), backwards);
    registry.setPackPresetsForFamily(ShaderFamily::Pointer, {}, backwardsByPack);
    // The DIAGNOSTIC is pinned too, like every other warning-emitting slot here: a fix
    // that silently stopped warning would otherwise pass, and the warning is how a pack
    // author learns their range is backwards.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("declares an inverted range")));
    QCOMPARE(registry
                 .resolveParams(ShaderFamily::Pointer, QStringLiteral("inverted"), QString(),
                                QVariantMap{{QStringLiteral("span"), 3}})
                 .value(QStringLiteral("span"))
                 .toInt(),
             3);
}

QTEST_MAIN(TestShaderPresetStore)
#include "test_shaderpresetstore.moc"
