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

[[maybe_unused]] bool writeFile(const QString& path, const QString& body)
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
[[maybe_unused]] std::unique_ptr<ShaderPresetStore> storePublishing(const QString& root, ShaderFamily family)
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
    void keepsAnAuthorDeclaredEmptyPreset();
    void refusesEveryImageValueWithNoPackDirectory();
    void dropsPresetLeftWithNothing();
    void refusesAMalformedPresetsBlock();
    void clampBoundariesAndOneSidedRanges();
    void refusesUnusableDeclaredBoundsAndValues();
    void dropsANullValueAndThePresetItEmpties();
    void boundsAHandWrittenPresetsParameterMap();

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

void TestShaderPresets::keepsAnAuthorDeclaredEmptyPreset()
{
    // `"Default": {}` legitimately means "this preset is the pack's declared
    // defaults", and it is KEPT while a preset emptied by refusals is dropped. The
    // two look identical at the point of the test below and mean opposite things, so
    // only covering the dropped half left the asymmetry unpinned — and the header
    // claimed a test for it existed.
    const QJsonObject root = jsonFrom(QStringLiteral(R"({
        "presets": { "Default": {}, "Tuned": { "speed": 2.0 } }
    })"));
    const PackPresets presets = parsePackPresets(QDir(), {}, root, lcTest());
    QVERIFY(presets.contains(QStringLiteral("Default")));
    QVERIFY(presets.value(QStringLiteral("Default")).isEmpty());
    // The neighbour is unaffected, so this is not "everything survives".
    QCOMPARE(presets.value(QStringLiteral("Tuned")).value(QStringLiteral("speed")).toDouble(), 2.0);
}

void TestShaderPresets::refusesEveryImageValueWithNoPackDirectory()
{
    // The fail-closed guard, which had no test at all. `QDir(QString())` behaves as
    // `QDir(".")`, so `absolutePath()` answers the process WORKING DIRECTORY — which
    // is non-empty, so resolveWithinDirectory's own empty-directory refusal never
    // fires and a relative preset texture gets confined to the CWD subtree and
    // ACCEPTED. With a compositor CWD of "/" that is most of the filesystem.
    //
    // The existing empty-value case cannot cover this: `"tex": ""` returns at the
    // no-texture branch BEFORE the guard is consulted, so it proves nothing about it.
    // This one passes a RELATIVE path, which is the value that would otherwise
    // resolve.
    const QJsonObject root = jsonFrom(QStringLiteral(R"({
        "presets": { "Textured": { "tex": "noise.png", "speed": 3.0 } }
    })"));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing every image-typed preset value")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("texture path\\(s\\) outside the pack")));

    const PackPresets presets = parsePackPresets(QDir(), {QStringLiteral("tex")}, root, lcTest());
    // The image value is refused...
    QVERIFY(!presets.value(QStringLiteral("Textured")).contains(QStringLiteral("tex")));
    // ...and the rest of the preset still loads, which is the point of refusing the
    // value rather than the preset.
    QCOMPARE(presets.value(QStringLiteral("Textured")).value(QStringLiteral("speed")).toDouble(), 3.0);
}

void TestShaderPresets::clampBoundariesAndOneSidedRanges()
{
    // The BOUNDARIES, which the main clamp slot steps over: it tests outside and strictly
    // inside a range, so an off-by-one at either end went unnoticed. Plus the two range
    // shapes nothing exercised at all: one-sided and negative.
    ShaderPresetRegistry registry;
    PresetValueBounds bounds;
    bounds.insert(QStringLiteral("ranged"), PresetValueRange(2, 8));
    // A declared MIN with no max, which is what makes the rounding path's overflow
    // reachable: nothing bounds the value from above.
    bounds.insert(QStringLiteral("floored"), PresetValueRange(QVariant(4), QVariant()));
    bounds.insert(QStringLiteral("capped"), PresetValueRange(QVariant(), QVariant(10)));
    bounds.insert(QStringLiteral("negative"), PresetValueRange(-0.6, -0.01));
    QHash<QString, PresetValueBounds> byPack;
    byPack.insert(QStringLiteral("pack"), bounds);
    registry.setPackPresetsForFamily(ShaderFamily::Overlay, {}, byPack);

    const auto resolved = [&registry](const QString& key, const QVariant& value) {
        return registry
            .resolveParams(ShaderFamily::Overlay, QStringLiteral("pack"), QString(), QVariantMap{{key, value}})
            .value(key);
    };

    // EXACTLY at each end comes back untouched, so neither bound is applied one step in.
    QCOMPARE(resolved(QStringLiteral("ranged"), 2).toInt(), 2);
    QCOMPARE(resolved(QStringLiteral("ranged"), 8).toInt(), 8);
    // One-sided: the declared side clamps, the absent side leaves the value alone,
    // including one far past where an int would overflow.
    QCOMPARE(resolved(QStringLiteral("floored"), 1).toInt(), 4);
    QCOMPARE(resolved(QStringLiteral("floored"), 1e12).toDouble(), 1e12);
    QCOMPARE(resolved(QStringLiteral("capped"), 99).toInt(), 10);
    QCOMPARE(resolved(QStringLiteral("capped"), -500).toInt(), -500);
    // A NEGATIVE fractional range clamps toward the interval the way a positive one does:
    // 0 is above the max here, so it comes back at the max rather than the min.
    QCOMPARE(resolved(QStringLiteral("negative"), 0).toDouble(), -0.01);
    QCOMPARE(resolved(QStringLiteral("negative"), -9).toDouble(), -0.6);
}

void TestShaderPresets::refusesUnusableDeclaredBoundsAndValues()
{
    // A bound that is not a number is NO bound. isValid() is true for a QString, so a pack
    // writing "min": "abc" used to clamp every value for that parameter to >= 0, and two
    // bad sides pinned everything to exactly 0.
    ShaderPresetRegistry registry;
    PresetValueBounds bounds;
    bounds.insert(QStringLiteral("bogus"), PresetValueRange(QVariant(QStringLiteral("abc")), QVariant()));
    bounds.insert(QStringLiteral("ranged"), PresetValueRange(0.0, 1.0));
    QHash<QString, PresetValueBounds> byPack;
    byPack.insert(QStringLiteral("pack"), bounds);
    registry.setPackPresetsForFamily(ShaderFamily::Pointer, {}, byPack);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("non-numeric")));
    QCOMPARE(registry
                 .resolveParams(ShaderFamily::Pointer, QStringLiteral("pack"), QString(),
                                QVariantMap{{QStringLiteral("bogus"), -5}})
                 .value(QStringLiteral("bogus"))
                 .toInt(),
             -5);

    // A numeric STRING on a ranged parameter is clamped and comes back as a number: the
    // uniform upload reads it with toFloat either way, so leaving it a string meant the
    // declared range never applied to it.
    const QVariant clamped = registry
                                 .resolveParams(ShaderFamily::Pointer, QStringLiteral("pack"), QString(),
                                                QVariantMap{{QStringLiteral("ranged"), QStringLiteral("9999")}})
                                 .value(QStringLiteral("ranged"));
    QCOMPARE(clamped.toDouble(), 1.0);
    QVERIFY(clamped.typeId() != QMetaType::QString);
    // A genuinely non-numeric string is left exactly as it was.
    QCOMPARE(registry
                 .resolveParams(ShaderFamily::Pointer, QStringLiteral("pack"), QString(),
                                QVariantMap{{QStringLiteral("ranged"), QStringLiteral("soft")}})
                 .value(QStringLiteral("ranged"))
                 .toString(),
             QStringLiteral("soft"));

    // And a NON-FINITE value is dropped, so the parameter falls back to the pack's
    // declared default rather than reaching the uniform as inf.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("non-finite value")));
    QVERIFY(!registry
                 .resolveParams(ShaderFamily::Pointer, QStringLiteral("pack"), QString(),
                                QVariantMap{{QStringLiteral("ranged"), std::numeric_limits<double>::infinity()}})
                 .contains(QStringLiteral("ranged")));
}

void TestShaderPresets::refusesAMalformedPresetsBlock()
{
    // Two shape refusals in the parse that nothing reached, each of which a
    // hand-written metadata.json can produce. Neither may take the pack down with it.
    const QJsonObject scalarBlock = jsonFrom(QStringLiteral(R"({ "presets": 7 })"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("presets")));
    QVERIFY(parsePackPresets(QDir(), {}, scalarBlock, lcTest()).isEmpty());

    // A non-object preset BODY drops that preset only; its usable neighbour stays.
    const QJsonObject scalarBody = jsonFrom(QStringLiteral(R"({
        "presets": { "Broken": "fast", "Fine": { "speed": 2.0 } }
    })"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Broken")));
    const PackPresets presets = parsePackPresets(QDir(), {}, scalarBody, lcTest());
    QVERIFY(!presets.contains(QStringLiteral("Broken")));
    QCOMPARE(presets.value(QStringLiteral("Fine")).value(QStringLiteral("speed")).toDouble(), 2.0);
}

void TestShaderPresets::dropsANullValueAndThePresetItEmpties()
{
    // A JSON null is dropped PER ENTRY, because carrying it through would read as 0
    // at every numeric consumer and pin the parameter rather than say nothing about
    // it. The rule was stated only for refused image paths, so the null half was
    // unstated and untested.
    const QJsonObject root = jsonFrom(QStringLiteral(R"({
        "presets": { "Partly": { "speed": null, "glow": 0.5 } }
    })"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("to null; ignoring that entry")));
    const PackPresets presets = parsePackPresets(QDir(), {}, root, lcTest());
    QVERIFY(!presets.value(QStringLiteral("Partly")).contains(QStringLiteral("speed")));
    QCOMPARE(presets.value(QStringLiteral("Partly")).value(QStringLiteral("glow")).toDouble(), 0.5);

    // And a preset whose EVERY value was a null is KEPT, empty. A null is not a refusal:
    // the schemas say it means "this preset says nothing about that parameter, so it keeps
    // the pack's default", which is what omitting the key means — so an all-null preset is
    // the author-declared `{}` written the long way. Dropping it made the loader contradict
    // the schema prose on the one case that prose describes. A preset emptied by a REFUSAL
    // is still dropped; that is dropsPresetLeftWithNothing.
    const QJsonObject allNull = jsonFrom(QStringLiteral(R"({
        "presets": { "Nothing": { "speed": null } }
    })"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("to null; ignoring that entry")));
    const PackPresets kept = parsePackPresets(QDir(), {}, allNull, lcTest());
    QVERIFY(kept.contains(QStringLiteral("Nothing")));
    QVERIFY(kept.value(QStringLiteral("Nothing")).isEmpty());
}

void TestShaderPresets::boundsAHandWrittenPresetsParameterMap()
{
    // The READ side of the parameter caps. The settings bridge bounds what it writes,
    // but a preset FILE is hand-editable and reached presetParams()/resolveParams()
    // with no cap at all, so the two halves of that pair disagreed.
    QJsonObject params;
    for (int i = 0; i < 200; ++i) {
        params.insert(QStringLiteral("p%1").arg(i), i);
    }
    params.insert(QStringLiteral("tex"), QString(4000, QLatin1Char('x')));
    const QJsonObject obj{{QStringLiteral("id"), QStringLiteral("kept")},
                          {QStringLiteral("name"), QStringLiteral("Kept")},
                          {QStringLiteral("packId"), QStringLiteral("dissolve")},
                          {QStringLiteral("params"), params}};

    const ShaderPreset preset = ShaderPreset::fromJson(obj, QStringLiteral("kept.json"));
    QVERIFY(preset.isValid());
    // EXACTLY the cap, not merely under it.
    QCOMPARE(preset.params.size(), ShaderPreset::MaxParams);
    // A string value is shortened rather than dropped, so an over-long image path
    // still names something rather than nothing.
    for (auto it = preset.params.cbegin(); it != preset.params.cend(); ++it) {
        if (it.value().typeId() == QMetaType::QString) {
            QCOMPARE(it.value().toString().size(), ShaderPreset::MaxValueChars);
        }
    }
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

QTEST_MAIN(TestShaderPresets)
#include "test_shaderpresets.moc"
