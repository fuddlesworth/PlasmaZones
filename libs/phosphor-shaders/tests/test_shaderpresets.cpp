// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_shaderpresets.cpp
 * @brief The preset parse / store / resolve contract, for every family.
 *
 * Covers the three things a preset assignment depends on being true:
 * pack-declared presets parse (including the containment refusal that keeps a
 * pack from binding a file outside itself), `preset + deltas` resolves the same
 * way everywhere, and a preset file edited on disk reaches the registry — which
 * is what makes an assignment react to a preset change without a restart.
 */

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

class TestShaderPresets : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ─────── parsePackPresets ───────

    void parsesNamedPresets();
    void absentPresetsBlockIsEmpty();
    void keepsUndeclaredParamIds();
    void refusesEscapingImagePathAndDropsIt();
    void carriesEmptyImageValueThrough();
    void dropsPresetLeftWithNothing();

    // ─────── overlayPresetDeltas / resolveParams ───────

    void deltasOverridePresetValues();
    void deltaWinsEvenWhenEqual();
    void resolveWithNoPresetIsJustDeltas();
    void resolveWithUnknownPresetIsJustDeltas();

    // ─────── registry ───────

    void userPresetShadowsPackDeclared();
    void packReloadWithNoChangeDoesNotSignal();
    void deletingLastUserPresetSignalsThatPack();
    void familiesAreSeparateNamespaces();
    void presetByIdFindsAUserPresetInAnyPack();
    void presetByIdPrefersTheUserPresetOverAPackDeclaredId();
    void presetByIdIsDeterministicWhenTwoPacksShareAnId();
    void presetByIdIsEmptyForAnEmptyOrUnknownId();

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

// ═══════════════════════════ parsePackPresets ═══════════════════════════

void TestShaderPresets::parsesNamedPresets()
{
    const QJsonObject root = jsonFrom(QStringLiteral(R"({
        "presets": {
            "Soft": { "speed": 0.5, "glow": 0.2 },
            "Harsh": { "speed": 3.0 }
        }
    })"));

    const PackPresets presets = parsePackPresets(QDir(QStringLiteral("/nonexistent")), {}, root, lcTest());

    QCOMPARE(presets.size(), 2);
    QCOMPARE(presets.value(QStringLiteral("Soft")).value(QStringLiteral("speed")).toDouble(), 0.5);
    QCOMPARE(presets.value(QStringLiteral("Soft")).value(QStringLiteral("glow")).toDouble(), 0.2);
    QCOMPARE(presets.value(QStringLiteral("Harsh")).size(), 1);
    QCOMPARE(presets.value(QStringLiteral("Harsh")).value(QStringLiteral("speed")).toDouble(), 3.0);
}

void TestShaderPresets::absentPresetsBlockIsEmpty()
{
    // The overwhelmingly common case: no pack ships presets today.
    const QJsonObject root = jsonFrom(QStringLiteral(R"({ "id": "dissolve", "name": "Dissolve" })"));
    QVERIFY(parsePackPresets(QDir(), {}, root, lcTest()).isEmpty());
}

void TestShaderPresets::keepsUndeclaredParamIds()
{
    // A preset naming a parameter the pack does not declare is inert at resolve
    // time, not dangerous. Dropping it here would need the declared-parameter
    // list, and would silently discard a value the offline validator is the
    // right place to complain about.
    const QJsonObject root = jsonFrom(QStringLiteral(R"({
        "presets": { "Odd": { "noSuchParam": 7 } }
    })"));
    const PackPresets presets = parsePackPresets(QDir(), {}, root, lcTest());
    QCOMPARE(presets.value(QStringLiteral("Odd")).value(QStringLiteral("noSuchParam")).toInt(), 7);
}

void TestShaderPresets::refusesEscapingImagePathAndDropsIt()
{
    QTemporaryDir packDir;
    QVERIFY(packDir.isValid());

    const QJsonObject root = jsonFrom(QStringLiteral(R"({
        "presets": {
            "Leaky": { "tex": "../../../etc/passwd", "speed": 2.0 }
        }
    })"));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("declared a path outside its own directory")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("texture path\\(s\\) outside the pack")));

    const PackPresets presets = parsePackPresets(QDir(packDir.path()), {QStringLiteral("tex")}, root, lcTest());

    // The escaping value is DROPPED so the parameter falls back to its declared
    // default, rather than binding an arbitrary file. The rest of the preset
    // survives — one bad entry does not discard the author's other values.
    const QVariantMap leaky = presets.value(QStringLiteral("Leaky"));
    QVERIFY(!leaky.contains(QStringLiteral("tex")));
    QCOMPARE(leaky.value(QStringLiteral("speed")).toDouble(), 2.0);
}

void TestShaderPresets::carriesEmptyImageValueThrough()
{
    // Empty is "no texture for this slot", a deliberate choice, not an escape.
    const QJsonObject root = jsonFrom(QStringLiteral(R"({
        "presets": { "Bare": { "tex": "" } }
    })"));
    const PackPresets presets = parsePackPresets(QDir(), {QStringLiteral("tex")}, root, lcTest());
    QVERIFY(presets.value(QStringLiteral("Bare")).contains(QStringLiteral("tex")));
    QVERIFY(presets.value(QStringLiteral("Bare")).value(QStringLiteral("tex")).toString().isEmpty());
}

void TestShaderPresets::dropsPresetLeftWithNothing()
{
    QTemporaryDir packDir;
    QVERIFY(packDir.isValid());
    const QJsonObject root = jsonFrom(QStringLiteral(R"({
        "presets": { "AllBad": { "tex": "/etc/passwd" } }
    })"));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("declared a path outside its own directory")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("texture path\\(s\\) outside the pack")));

    const PackPresets presets = parsePackPresets(QDir(packDir.path()), {QStringLiteral("tex")}, root, lcTest());
    // Nothing survived, so the preset itself is gone rather than being offered
    // as an empty entry that would silently do nothing when picked.
    QVERIFY(!presets.contains(QStringLiteral("AllBad")));
}

// ═══════════════════════ resolution ═══════════════════════

void TestShaderPresets::deltasOverridePresetValues()
{
    const QVariantMap base{{QStringLiteral("speed"), 1.0}, {QStringLiteral("glow"), 0.2}};
    const QVariantMap deltas{{QStringLiteral("glow"), 0.9}};

    const QVariantMap merged = overlayPresetDeltas(base, deltas);
    QCOMPARE(merged.size(), 2);
    // Untouched param follows the preset...
    QCOMPARE(merged.value(QStringLiteral("speed")).toDouble(), 1.0);
    // ...the tweaked one stays tweaked.
    QCOMPARE(merged.value(QStringLiteral("glow")).toDouble(), 0.9);
}

void TestShaderPresets::deltaWinsEvenWhenEqual()
{
    // An explicit override whose value happens to equal the preset's is still
    // an override: the preset may change later, and the user's choice must not
    // silently start following it.
    //
    // Asserting on the value alone could not fail — both sides are 1.0, so it
    // held whether or not the overlay ran at all. What the contract is actually
    // about is key RETENTION, so assert that: the delta key is present in the
    // result, and the preset's own later value does NOT reach a key the
    // assignment pinned.
    const QVariantMap base{{QStringLiteral("speed"), 1.0}, {QStringLiteral("glow"), 0.2}};
    const QVariantMap deltas{{QStringLiteral("speed"), 1.0}};
    const QVariantMap merged = overlayPresetDeltas(base, deltas);
    QVERIFY(merged.contains(QStringLiteral("speed")));
    QCOMPARE(merged.value(QStringLiteral("speed")).toDouble(), 1.0);

    // The pin is only observable against a RETUNED preset: the same delta over a
    // preset that has since moved must still answer with the user's value.
    const QVariantMap retuned{{QStringLiteral("speed"), 4.0}, {QStringLiteral("glow"), 0.2}};
    QCOMPARE(overlayPresetDeltas(retuned, deltas).value(QStringLiteral("speed")).toDouble(), 1.0);
    // ...while a key the user never touched does follow the retune.
    QCOMPARE(overlayPresetDeltas(retuned, deltas).value(QStringLiteral("glow")).toDouble(), 0.2);
}

void TestShaderPresets::resolveWithNoPresetIsJustDeltas()
{
    ShaderPresetRegistry registry;
    const QVariantMap deltas{{QStringLiteral("speed"), 2.0}};
    QCOMPARE(registry.resolveParams(ShaderFamily::Animation, QStringLiteral("dissolve"), QString(), deltas), deltas);
}

void TestShaderPresets::resolveWithUnknownPresetIsJustDeltas()
{
    // An assignment outliving its preset degrades to the look it would have had
    // with no preset at all. It must never resolve to nothing.
    ShaderPresetRegistry registry;
    const QVariantMap deltas{{QStringLiteral("speed"), 2.0}};
    QCOMPARE(registry.resolveParams(ShaderFamily::Animation, QStringLiteral("dissolve"), QStringLiteral("{deleted}"),
                                    deltas),
             deltas);
}

// ═══════════════════════ registry ═══════════════════════

void TestShaderPresets::userPresetShadowsPackDeclared()
{
    ShaderPresetRegistry registry;
    registry.setPackPresets(ShaderFamily::Surface, QStringLiteral("border"),
                            PackPresets{{QStringLiteral("Soft"), {{QStringLiteral("width"), 1}}}});

    ShaderPreset mine;
    mine.id = QStringLiteral("Soft"); // same id as the pack-declared one
    mine.name = QStringLiteral("Soft");
    mine.packId = QStringLiteral("border");
    mine.params = {{QStringLiteral("width"), 8}};
    registry.setUserPresets(ShaderFamily::Surface, {mine});

    const ShaderPreset found = registry.preset(ShaderFamily::Surface, QStringLiteral("border"), QStringLiteral("Soft"));
    QCOMPARE(found.params.value(QStringLiteral("width")).toInt(), 8);
    QVERIFY(!found.readOnly);
    // The shadowed pack preset is not ALSO offered, or the picker would show
    // two rows with one id.
    QCOMPARE(registry.presetsFor(ShaderFamily::Surface, QStringLiteral("border")).size(), 1);
}

void TestShaderPresets::packReloadWithNoChangeDoesNotSignal()
{
    // presetsChanged drops every compiled surface pack in the compositor, so a
    // pack rescan that changed nothing must stay silent.
    ShaderPresetRegistry registry;
    const PackPresets presets{{QStringLiteral("Soft"), {{QStringLiteral("width"), 1}}}};
    registry.setPackPresets(ShaderFamily::Surface, QStringLiteral("border"), presets);

    QSignalSpy spy(&registry, &ShaderPresetRegistry::presetsChanged);
    registry.setPackPresets(ShaderFamily::Surface, QStringLiteral("border"), presets);
    QCOMPARE(spy.count(), 0);

    registry.setPackPresets(ShaderFamily::Surface, QStringLiteral("border"),
                            PackPresets{{QStringLiteral("Soft"), {{QStringLiteral("width"), 2}}}});
    QCOMPARE(spy.count(), 1);
}

void TestShaderPresets::deletingLastUserPresetSignalsThatPack()
{
    ShaderPresetRegistry registry;
    ShaderPreset mine;
    mine.id = QStringLiteral("{a}");
    mine.name = QStringLiteral("Mine");
    mine.packId = QStringLiteral("dissolve");
    registry.setUserPresets(ShaderFamily::Animation, {mine});

    QSignalSpy spy(&registry, &ShaderPresetRegistry::presetsChanged);
    registry.setUserPresets(ShaderFamily::Animation, {});
    // A pack whose last preset just vanished still has to hear about it, or a
    // picker keeps offering a preset that no longer exists.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("dissolve"));
    QVERIFY(registry.presetsFor(ShaderFamily::Animation, QStringLiteral("dissolve")).isEmpty());
}

void TestShaderPresets::familiesAreSeparateNamespaces()
{
    // The same pack id in two families is two different packs.
    ShaderPresetRegistry registry;
    registry.setPackPresets(ShaderFamily::Animation, QStringLiteral("glow"),
                            PackPresets{{QStringLiteral("A"), {{QStringLiteral("x"), 1}}}});
    QVERIFY(registry.presetsFor(ShaderFamily::Surface, QStringLiteral("glow")).isEmpty());
    QCOMPARE(registry.presetsFor(ShaderFamily::Animation, QStringLiteral("glow")).size(), 1);
}

// `presetById` is the lookup the whole write side routes through: rename,
// update and delete each resolve the stored record by id alone, because the
// editor knows the id it is acting on and not which pack declared it.

void TestShaderPresets::presetByIdFindsAUserPresetInAnyPack()
{
    ShaderPresetRegistry registry;
    ShaderPreset mine;
    mine.id = QStringLiteral("{a}");
    mine.name = QStringLiteral("Mine");
    mine.packId = QStringLiteral("dissolve");
    mine.params = {{QStringLiteral("speed"), 2.0}};
    registry.setUserPresets(ShaderFamily::Animation, {mine});

    const ShaderPreset found = registry.presetById(ShaderFamily::Animation, QStringLiteral("{a}"));
    QVERIFY(found.isValid());
    QCOMPARE(found.packId, QStringLiteral("dissolve"));
    QCOMPARE(found.params.value(QStringLiteral("speed")).toDouble(), 2.0);

    // Family-scoped, like every other lookup here: the same id in another
    // family is another preset.
    QVERIFY(!registry.presetById(ShaderFamily::Surface, QStringLiteral("{a}")).isValid());
}

void TestShaderPresets::presetByIdPrefersTheUserPresetOverAPackDeclaredId()
{
    // Same precedence as the pack-scoped lookup, and it matters more here: an
    // update resolving to the read-only pack record would write the pack's
    // values back out under the user's id.
    ShaderPresetRegistry registry;
    registry.setPackPresets(ShaderFamily::Surface, QStringLiteral("border"),
                            PackPresets{{QStringLiteral("Soft"), {{QStringLiteral("width"), 1}}}});

    ShaderPreset mine;
    mine.id = QStringLiteral("Soft");
    mine.name = QStringLiteral("Soft");
    mine.packId = QStringLiteral("border");
    mine.params = {{QStringLiteral("width"), 8}};
    registry.setUserPresets(ShaderFamily::Surface, {mine});

    const ShaderPreset found = registry.presetById(ShaderFamily::Surface, QStringLiteral("Soft"));
    QVERIFY(!found.readOnly);
    QCOMPARE(found.params.value(QStringLiteral("width")).toInt(), 8);
}

void TestShaderPresets::presetByIdIsDeterministicWhenTwoPacksShareAnId()
{
    // Ids are supposed to be unique within a family, but a user preset's id
    // comes from a hand-editable file, so a collision is reachable. Which
    // record answers must not depend on QHash iteration order, or a rename
    // would move a different preset between two runs of the same binary.
    ShaderPresetRegistry registry;
    ShaderPreset first;
    first.id = QStringLiteral("{dup}");
    first.name = QStringLiteral("First");
    first.packId = QStringLiteral("aaa-pack");
    first.params = {{QStringLiteral("speed"), 1.0}};
    ShaderPreset second = first;
    second.name = QStringLiteral("Second");
    second.packId = QStringLiteral("zzz-pack");
    second.params = {{QStringLiteral("speed"), 9.0}};
    registry.setUserPresets(ShaderFamily::Animation, {first, second});

    // Sorted scope keys, so the lowest pack id answers, every time.
    for (int run = 0; run < 4; ++run) {
        const ShaderPreset found = registry.presetById(ShaderFamily::Animation, QStringLiteral("{dup}"));
        QCOMPARE(found.packId, QStringLiteral("aaa-pack"));
        QCOMPARE(found.params.value(QStringLiteral("speed")).toDouble(), 1.0);
    }
}

void TestShaderPresets::presetByIdIsEmptyForAnEmptyOrUnknownId()
{
    ShaderPresetRegistry registry;
    registry.setPackPresets(ShaderFamily::Animation, QStringLiteral("dissolve"),
                            PackPresets{{QStringLiteral("Soft"), {{QStringLiteral("speed"), 1.0}}}});
    QVERIFY(!registry.presetById(ShaderFamily::Animation, QString()).isValid());
    QVERIFY(!registry.presetById(ShaderFamily::Animation, QStringLiteral("nope")).isValid());
    // The pack-declared one IS reachable by id, and carries its read-only mark
    // so the write side can refuse it.
    const ShaderPreset packed = registry.presetById(ShaderFamily::Animation, QStringLiteral("Soft"));
    QVERIFY(packed.isValid());
    QVERIFY(packed.readOnly);
}

// ═══════════════════════ loader ═══════════════════════

void TestShaderPresets::loadsPresetFilesFromDisk()
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

void TestShaderPresets::filenameStemIsTheFallbackId()
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

void TestShaderPresets::skipsPresetWithNoPackId()
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

void TestShaderPresets::editingAPresetFileReachesTheRegistry()
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

void TestShaderPresets::migratesLegacyOverlayPreset()
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

void TestShaderPresets::migrationIsIdempotent()
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

void TestShaderPresets::migratedIdIsDerivedNotRandom()
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

void TestShaderPresets::migrationLeavesForeignFilesAlone()
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

void TestShaderPresets::migrationFallsBackToFilenameForName()
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

void TestShaderPresets::migrationOnAbsentRootIsNoOp()
{
    QCOMPARE(migrateLegacyOverlayPresets(QStringLiteral("/nonexistent/plasmazones/shader-presets")), 0);
}

void TestShaderPresets::watcherSeesAPlainRewrite()
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

void TestShaderPresets::watcherSeesAnAtomicRenameSave()
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

void TestShaderPresets::watcherSeesAPresetAddedToAFreshInstall()
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

void TestShaderPresets::storeDestructionDoesNotTouchAFreedRegistry()
{
    // No test constructed a ShaderPresetStore at all, which is why a
    // use-after-free on its teardown was invisible to a green suite: the store
    // parented both the registry and its loaders to itself, and QObject frees
    // children in insertion order, so the registry died first while each
    // loader's destructor was still retracting through it.
    //
    // The fault is now STRUCTURALLY unreachable rather than ordered-around. The
    // registry is a by-value member declared before the publisher slots, so it
    // outlives them under ordinary member-destruction rules, and the retraction
    // happens in the store's own destructor body while it is provably alive.
    // There is no QObject child destruction in the path at all, and nothing for
    // a QPointer to catch — which is why there is no longer a QPointer.
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

void TestShaderPresets::storeLoadIsIdempotent()
{
    // A second load() used to build four more loaders, leak the first four with
    // their watchers armed, and leave two publishers per family — which the
    // loader destructor's whole-family retraction cannot survive.
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

void TestShaderPresets::storeRefusesANonAbsoluteRoot()
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

void TestShaderPresets::anIdThatIsNotAPathComponentIsRefused()
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

void TestShaderPresets::packPresetsAreRetractedForAVanishedPack()
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

void TestShaderPresets::storeLoadsOnlyTheFamiliesTheConsumerNames()
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

void TestShaderPresets::resolveParamsClampsToTheDeclaredRange()
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
}

QTEST_MAIN(TestShaderPresets)
#include "test_shaderpresets.moc"
