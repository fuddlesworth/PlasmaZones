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
#include <PhosphorShaders/ShaderPresetLoader.h>
#include <PhosphorShaders/ShaderPresetParse.h>
#include <PhosphorShaders/ShaderPresetRegistry.h>

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
    const QVariantMap base{{QStringLiteral("speed"), 1.0}};
    const QVariantMap deltas{{QStringLiteral("speed"), 1.0}};
    QCOMPARE(overlayPresetDeltas(base, deltas).value(QStringLiteral("speed")).toDouble(), 1.0);
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

    ShaderPresetRegistry registry;
    ShaderPresetLoader loader(registry, ShaderFamily::Animation);
    loader.loadFromDirectory(dir, LiveReload::Off);

    const QList<ShaderPreset> presets = registry.presetsFor(ShaderFamily::Animation, QStringLiteral("dissolve"));
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

    ShaderPresetRegistry registry;
    ShaderPresetLoader loader(registry, ShaderFamily::Pointer);
    loader.loadFromDirectory(dir, LiveReload::Off);

    const QList<ShaderPreset> presets = registry.presetsFor(ShaderFamily::Pointer, QStringLiteral("trail"));
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

    ShaderPresetRegistry registry;
    ShaderPresetLoader loader(registry, ShaderFamily::Overlay);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("preset file with no pack id")));
    loader.loadFromDirectory(dir, LiveReload::Off);

    // A preset naming no pack cannot be offered anywhere: parameter ids mean
    // nothing across packs.
    QVERIFY(registry.presetsFor(ShaderFamily::Overlay, QStringLiteral("aurora")).isEmpty());
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

    ShaderPresetRegistry registry;
    ShaderPresetLoader loader(registry, ShaderFamily::Animation);
    loader.loadFromDirectory(dir, LiveReload::Off);

    const QVariantMap deltas{{QStringLiteral("glow"), 0.5}};
    QVariantMap resolved =
        registry.resolveParams(ShaderFamily::Animation, QStringLiteral("dissolve"), QStringLiteral("{neon}"), deltas);
    QCOMPARE(resolved.value(QStringLiteral("speed")).toDouble(), 1.0);
    QCOMPARE(resolved.value(QStringLiteral("glow")).toDouble(), 0.5);

    QSignalSpy spy(&registry, &ShaderPresetRegistry::presetsChanged);
    QVERIFY(writeFile(file, QStringLiteral(R"({
        "id": "{neon}", "name": "Neon", "packId": "dissolve", "params": { "speed": 9.0 }
    })")));
    loader.rescanNow();

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
    ShaderPresetRegistry registry;
    ShaderPresetLoader loader(registry, ShaderFamily::Overlay);
    loader.loadFromDirectory(userPresetDirectory(root.path(), ShaderFamily::Overlay), LiveReload::Off);

    const QList<ShaderPreset> presets = registry.presetsFor(ShaderFamily::Overlay, QStringLiteral("aurora"));
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

    ShaderPresetRegistry registry;
    ShaderPresetLoader loader(registry, ShaderFamily::Overlay);
    loader.loadFromDirectory(userPresetDirectory(root.path(), ShaderFamily::Overlay), LiveReload::Off);
    // A nameless preset would render as a blank picker row; the old filename is
    // the best name available and is what the user recognises.
    QCOMPARE(registry.presetsFor(ShaderFamily::Overlay, QStringLiteral("aurora")).at(0).name,
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

    ShaderPresetRegistry registry;
    ShaderPresetLoader loader(registry, ShaderFamily::Animation);
    loader.loadFromDirectory(dir, LiveReload::On);
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

    ShaderPresetRegistry registry;
    ShaderPresetLoader loader(registry, ShaderFamily::Animation);
    loader.loadFromDirectory(dir, LiveReload::On);

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

    ShaderPresetRegistry registry;
    ShaderPresetLoader loader(registry, ShaderFamily::Animation);
    loader.loadFromDirectory(dir, LiveReload::On);

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

QTEST_MAIN(TestShaderPresets)
#include "test_shaderpresets.moc"
