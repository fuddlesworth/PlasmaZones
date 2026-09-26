// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ShellChrome turns the decoration tree the daemon publishes into the
// stage list SurfaceDecoration draws, one stage per installed pack in a
// surface's enabled chain, against the bundled packs in plasmazones/data/surface.

#include "ShellChrome.h"

#include <PhosphorShaders/ShaderPresetStore.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorShellApp;
using namespace PhosphorSurfaceShaders;

namespace {

QString treeJson(const QString& path, const QStringList& chain, const QVariantMap& params = {},
                 const QVariantMap& presetIds = {})
{
    DecorationProfileTree tree;
    DecorationProfile profile;
    profile.chain = chain;
    if (!params.isEmpty()) {
        profile.parameters = params;
    }
    if (!presetIds.isEmpty()) {
        profile.presetIds = presetIds;
    }
    tree.setOverride(path, profile);
    return QString::fromUtf8(QJsonDocument(tree.toJson()).toJson(QJsonDocument::Compact));
}

/// Write a user preset file for @p packId into the SURFACE family's directory under
/// the test's own XDG_DATA_HOME (phosphor_apply_test_isolation gives each target
/// one), which is exactly where ShellChrome's store scans.
bool writeSurfacePreset(const QString& presetId, const QString& packId, const QVariantMap& params)
{
    const QString dir = PhosphorShaders::userPresetDirectory(PhosphorShaders::standardUserPresetRoot(),
                                                             PhosphorShaders::ShaderFamily::Surface);
    if (!QDir().mkpath(dir)) {
        return false;
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), presetId);
    obj.insert(QStringLiteral("name"), presetId);
    obj.insert(QStringLiteral("packId"), packId);
    obj.insert(QStringLiteral("params"), QJsonObject::fromVariantMap(params));
    QFile file(dir + QLatin1Char('/') + presetId + QStringLiteral(".json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(QJsonDocument(obj).toJson()) > 0;
}

/// Write a pack into @p dir that DECLARES a preset, so the pack-declared half of the
/// preset feature has something to seed from.
///
/// No bundled surface pack declares presets (grep data/ — only phosphor-gate does, and it
/// is an animation pack), so without a fixture like this the `seedPackPresets` call in
/// ShellChrome's constructor can be deleted with this whole file still green.
bool writePackWithPreset(const QString& dir, const QString& packId, const QString& presetName, int glowSize)
{
    if (!QDir().mkpath(dir + QLatin1Char('/') + packId)) {
        return false;
    }
    QJsonObject param;
    param.insert(QStringLiteral("id"), QStringLiteral("glowSize"));
    param.insert(QStringLiteral("name"), QStringLiteral("glowSize"));
    param.insert(QStringLiteral("type"), QStringLiteral("float"));
    param.insert(QStringLiteral("default"), 24);
    param.insert(QStringLiteral("min"), 4);
    param.insert(QStringLiteral("max"), 64);
    QJsonObject metadata;
    metadata.insert(QStringLiteral("id"), packId);
    metadata.insert(QStringLiteral("name"), packId);
    metadata.insert(QStringLiteral("fragmentShader"), QStringLiteral("effect.frag"));
    metadata.insert(QStringLiteral("paddingParam"), QStringLiteral("glowSize"));
    metadata.insert(QStringLiteral("parameters"), QJsonArray{param});
    metadata.insert(QStringLiteral("presets"),
                    QJsonObject{{presetName, QJsonObject{{QStringLiteral("glowSize"), glowSize}}}});

    const auto write = [&](const QString& name, const QByteArray& body) {
        QFile file(dir + QLatin1Char('/') + packId + QLatin1Char('/') + name);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(body) > 0;
    };
    return write(QStringLiteral("metadata.json"), QJsonDocument(metadata).toJson())
        && write(
               QStringLiteral("effect.frag"),
               QByteArrayLiteral("vec4 pSurface(vec2 uv)\n{\n    return vec4(float(p_glowSize), 0.0, 0.0, 1.0);\n}\n"));
}

} // namespace

class TestShellChrome : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(QDir(QStringLiteral(PZ_BUNDLED_SURFACE_DIR)).exists(), "bundled surface packs not found");
    }

    // writeSurfacePreset() drops JSON into the target's XDG_DATA_HOME, which
    // the isolation sandbox gives us but does not clear between ctest runs.
    //
    // The hazard is cross-SLOT, not cross-run-of-this-slot: the slot that
    // retunes a preset writes its value fresh before constructing the
    // chrome, so its own assertions are safe either way. What a crashed or
    // interrupted run leaves behind is a preset visible to the OTHER slots,
    // which expect the pack defaults and would silently read the leftover
    // instead.
    void cleanup()
    {
        const QString dir = PhosphorShaders::userPresetDirectory(PhosphorShaders::standardUserPresetRoot(),
                                                                 PhosphorShaders::ShaderFamily::Surface);
        for (const QString& id : {QStringLiteral("shell-wide"), QStringLiteral("shell-live")}) {
            QFile::remove(QDir(dir).filePath(id + QStringLiteral(".json")));
        }
    }

    void emptyTreeDecoratesNothing()
    {
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVERIFY(chrome.chainFor(decorationShellPhosphorBarPath()).isEmpty());
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorBarPath()), 0.0);
    }

    void chainResolvesToOneStagePerInstalledPack()
    {
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QSignalSpy revised(&chrome, &ShellChrome::revisionChanged);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorBarPath(),
                                            {QStringLiteral("border-phosphor"), QStringLiteral("no-such-pack")})));
        QCOMPARE(revised.count(), 1);

        const QVariantList stages = chrome.chainFor(decorationShellPhosphorBarPath());
        QCOMPARE(stages.size(), 1);
        const QVariantMap stage = stages.first().toMap();
        QVERIFY(stage.contains(QStringLiteral("source")));
        QVERIFY(!stage.value(QStringLiteral("source")).toString().isEmpty());
        QVERIFY(stage.contains(QStringLiteral("params")));

        // Same tree again is a no-op.
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorBarPath(),
                                            {QStringLiteral("border-phosphor"), QStringLiteral("no-such-pack")})));
        QCOMPARE(revised.count(), 1);
    }

    /// The chain's bottom-corner answer has to reach the UPLOADED slot map of a pack
    /// that never set it, not merely come out of the resolver. Stored on the FIRST pack
    /// in chain order only, so anything the second pack draws at the bottom can only
    /// have come from the chain.
    void theChainsBottomCornerAnswerReachesEveryStage()
    {
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        const QStringList chain{QStringLiteral("blur"), QStringLiteral("border")};

        // blur squares its bottom; border stores nothing and declares the opposite.
        QVariantMap squaredTree;
        QVariantMap blurParams;
        blurParams.insert(QStringLiteral("roundBottomCorners"), false);
        squaredTree.insert(QStringLiteral("blur"), blurParams);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorBarPath(), chain, squaredTree)));
        const QVariantList squaredStages = chrome.chainFor(decorationShellPhosphorBarPath());
        QCOMPARE(squaredStages.size(), 2);
        const QVariantMap squaredBorder = squaredStages.at(1).toMap().value(QStringLiteral("params")).toMap();

        // Same chain, nothing stored: step 2 hands both packs a declared true.
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorBarPath(), chain)));
        const QVariantList roundedStages = chrome.chainFor(decorationShellPhosphorBarPath());
        QCOMPARE(roundedStages.size(), 2);
        const QVariantMap roundedBorder = roundedStages.at(1).toMap().value(QStringLiteral("params")).toMap();

        // Compared by DIFFERENCE, not by lane name: translateSurfaceParams numbers the
        // lanes from the pack's OWN declaration order, so naming one would break on an
        // unrelated reorder of border's parameters. Exactly one lane may move, and it
        // must flip 1.0 -> 0.0. With the injection at chainFor removed both maps carry
        // border's own default and NOTHING differs, so this fails rather than passing
        // vacuously.
        // Iterating the ROUNDED map's keys is sufficient only because both chains run the
        // same two packs in the same order, so the lane sets are identical and a lane
        // present in one is present in the other.
        QStringList changed;
        for (auto it = roundedBorder.constBegin(); it != roundedBorder.constEnd(); ++it) {
            const QVariant squaredValue = squaredBorder.value(it.key());
            if (squaredValue != it.value()) {
                changed << it.key();
                QCOMPARE(it.value().toDouble(), 1.0);
                QCOMPARE(squaredValue.toDouble(), 0.0);
            }
        }
        // Names the premise, because "Actual: 0 Expected: 1" on its own sends the reader
        // looking at the injection when the likelier cause is the fixture: this needs the
        // bundled blur and border packs to BOTH declare roundBottomCorners, and blur's
        // default to be true so squaring it is a change.
        QVERIFY2(changed.size() == 1,
                 qPrintable(QStringLiteral("expected exactly one differing lane, got %1. Do bundled blur and "
                                           "border both still declare roundBottomCorners, with blur defaulting "
                                           "to true?")
                                .arg(changed.size())));
    }

    void outerPaddingFollowsTheChainsLargestRequest()
    {
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVariantMap params;
        QVariantMap glow;
        glow.insert(QStringLiteral("glowSize"), 40);
        params.insert(QStringLiteral("glow"), glow);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, params)));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 40.0);
        // Another surface is untouched.
        QVERIFY(chrome.chainFor(decorationShellPhosphorBarPath()).isEmpty());
    }

    void outerPaddingTakesTheLargestOfSeveralPacks()
    {
        // The single-pack case above cannot tell a max-of-chain fold from a
        // last-one-wins, or from a first-one-wins. Two packs that both ask for
        // room, largest second and then largest first.
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVariantMap params;
        QVariantMap glow;
        glow.insert(QStringLiteral("glowSize"), 12);
        QVariantMap shadow;
        shadow.insert(QStringLiteral("shadowSize"), 30);
        params.insert(QStringLiteral("glow"), glow);
        params.insert(QStringLiteral("shadow"), shadow);
        QVERIFY(chrome.setTreeJson(
            treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow"), QStringLiteral("shadow")}, params)));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 30.0);

        QVariantMap swapped;
        QVariantMap bigGlow;
        bigGlow.insert(QStringLiteral("glowSize"), 44);
        QVariantMap smallShadow;
        smallShadow.insert(QStringLiteral("shadowSize"), 5);
        swapped.insert(QStringLiteral("glow"), bigGlow);
        swapped.insert(QStringLiteral("shadow"), smallShadow);
        QVERIFY(chrome.setTreeJson(
            treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow"), QStringLiteral("shadow")}, swapped)));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 44.0);
    }

    void outerPaddingSkipsAPackTheRegistryDoesNotHave()
    {
        // An unknown pack in the chain contributes nothing rather than
        // aborting the fold, so the real pack beside it still gets its room.
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVariantMap params;
        QVariantMap glow;
        glow.insert(QStringLiteral("glowSize"), 18);
        params.insert(QStringLiteral("glow"), glow);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(),
                                            {QStringLiteral("no-such-pack"), QStringLiteral("glow")}, params)));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 18.0);
    }

    void aPackDeclaredPresetSuppliesTheSurfacesParameters()
    {
        // The PACK-DECLARED half, which `seedPackPresets` is the only thing that puts in
        // the registry. The user-preset slots below cover the store's directory load; this
        // one covers the seed, and it needs its own fixture pack because no bundled surface
        // pack declares a preset.
        QTemporaryDir packs;
        QVERIFY(packs.isValid());
        QVERIFY(writePackWithPreset(packs.path(), QStringLiteral("seeded-glow"), QStringLiteral("Wide"), 52));

        ShellChrome chrome({packs.path()}, nullptr);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("seeded-glow")}, {},
                                            {{QStringLiteral("seeded-glow"), QStringLiteral("Wide")}})));
        // The PRESET's value, not the pack's declared default of 24.
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 52.0);
    }

    void aPresetSuppliesTheShellSurfacesParameters()
    {
        // The USER-preset half: the store's directory load plus the flatten. The
        // pack-declared half is the slot above, which needs its own fixture pack because
        // no bundled surface pack declares a preset — so between them the three arms
        // (store load, seedPackPresets, presetsChanged) are all covered, where before this
        // file mentioned no preset at all and any of the three could be deleted green.
        //
        // glow declares glowSize default 24, and it is the pack's paddingParam, so the
        // resolved padding is the cheapest observable for "which values did the chain
        // actually compose with".
        QVERIFY(writeSurfacePreset(QStringLiteral("shell-wide"), QStringLiteral("glow"),
                                   {{QStringLiteral("glowSize"), 48}}));

        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, {},
                                            {{QStringLiteral("glow"), QStringLiteral("shell-wide")}})));
        // The PRESET's value, not the pack's default of 24.
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 48.0);

        // An assignment's own value still wins over the preset, which is the delta rule
        // every other surface follows.
        QVariantMap own;
        own.insert(QStringLiteral("glow"), QVariantMap{{QStringLiteral("glowSize"), 12}});
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, own,
                                            {{QStringLiteral("glow"), QStringLiteral("shell-wide")}})));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 12.0);

        // And a preset id naming nothing degrades to the pack's default rather than
        // rendering nothing.
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, {},
                                            {{QStringLiteral("glow"), QStringLiteral("gone-away")}})));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 24.0);
    }

    void retuningAPresetOnDiskRevisesTheShellChrome()
    {
        // The `presetsChanged` → `bump()` connection, which is what makes a retune
        // reach a live shell surface. Without it the new values sit in the registry and
        // nothing re-reads them until something else happens to revise.
        QVERIFY(writeSurfacePreset(QStringLiteral("shell-live"), QStringLiteral("glow"),
                                   {{QStringLiteral("glowSize"), 40}}));

        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, {},
                                            {{QStringLiteral("glow"), QStringLiteral("shell-live")}})));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 40.0);

        QSignalSpy revised(&chrome, &ShellChrome::revisionChanged);
        QVERIFY(writeSurfacePreset(QStringLiteral("shell-live"), QStringLiteral("glow"),
                                   {{QStringLiteral("glowSize"), 56}}));
        // The store watches the directory, so the revision arrives asynchronously.
        QVERIFY2(revised.wait(5000), "a retuned preset must revise the chrome");
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 56.0);
    }

    void outerPaddingIsBoundedByTheDeclaredRange()
    {
        // A typo'd or hostile parameter cannot demand an unbounded canvas, and the
        // bound that stops it is now the PACK'S OWN declared range rather than the
        // padding ceiling. ShellChrome flattens presets before reading its parameters,
        // and `resolveParams` clamps every value to the range the pack declares — so
        // glow's `glowSize` (min 4, max 64) comes back at 64 and the 128px
        // kMaxDecorationOuterPaddingPx backstop is never reached through this path.
        //
        // That ordering is the point: the ceiling is a last resort for a parameter the
        // pack declares no max for, which clampToBounds deliberately leaves alone.
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVariantMap params;
        QVariantMap glow;
        glow.insert(QStringLiteral("glowSize"), 100000);
        params.insert(QStringLiteral("glow"), glow);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, params)));
        const double clamped = chrome.outerPaddingFor(decorationShellPhosphorOsdPath());
        QCOMPARE(clamped, 64.0);
        // Still under the ceiling, which is what makes the declared range the tighter
        // of the two bounds rather than a second copy of it.
        QVERIFY(clamped < static_cast<double>(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx));

        // And a negative request never shrinks the surface. It is clamped to the
        // pack's declared MIN of 4 on the way through, so the result is 4 rather
        // than 0: the declared range is the binding floor here, not the padding
        // request's own non-negative floor.
        QVariantMap negative;
        QVariantMap shrink;
        shrink.insert(QStringLiteral("glowSize"), -60);
        negative.insert(QStringLiteral("glow"), shrink);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, negative)));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 4.0);
    }

    void malformedJsonKeepsTheTree()
    {
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorBarPath(), {QStringLiteral("border-phosphor")})));
        QVERIFY(!chrome.setTreeJson(QStringLiteral("[not an object")));
        QCOMPARE(chrome.chainFor(decorationShellPhosphorBarPath()).size(), 1);
    }

    /// decorationReloadGeneration is a SEPARATE tick from `revision`, and this pins
    /// the separation in both directions.
    ///
    /// The counter exists because recomposing the chain does not cover an in-place
    /// edit of a pack's shader SOURCE: the composition comes out byte-identical,
    /// every stage rebinds the same URL, and nothing re-bakes. Folding it into
    /// bump() would instead re-bake every stage on every tree and palette change.
    ///
    /// It also covers the wiring, which is what this slot was written chasing: both
    /// registry connections used to live in subscribeToDaemon(), which only the
    /// OTHER constructor calls, so a chrome built with explicit search paths had
    /// neither and a live pack edit re-resolved nothing at all.
    void aRegistryRescanRebakesButATreeEditDoesNot()
    {
        QTemporaryDir packs;
        QVERIFY(packs.isValid());
        // The pack exists BEFORE the chrome does, so the edit below is a change to a
        // file the registry is already watching rather than a new subdirectory.
        QVERIFY(writePackWithPreset(packs.path(), QStringLiteral("reload-glow"), QStringLiteral("Wide"), 24));
        ShellChrome chrome({packs.path()}, nullptr);
        QCOMPARE(chrome.decorationReloadGeneration(), 0);

        QSignalSpy reloads(&chrome, &ShellChrome::decorationReloadGenerationChanged);
        QSignalSpy revised(&chrome, &ShellChrome::revisionChanged);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("reload-glow")})));
        QCOMPARE(chrome.chainFor(decorationShellPhosphorOsdPath()).size(), 1);

        // The tree edit DID revise the chrome and did NOT ask for a re-bake. Both
        // halves matter: without the first this passes on a chrome that noticed
        // nothing, and without the second it passes with the two counters merged.
        QVERIFY(revised.count() > 0);
        QCOMPARE(chrome.decorationReloadGeneration(), 0);
        QCOMPARE(reloads.count(), 0);

        // A live edit of the pack's SHADER SOURCE does ask for one. Written as an
        // atomic rename, which is how an editor saves and what the registry's
        // per-entry watches are documented to cover, and at a DIFFERENT LENGTH:
        // effectContentSignature mixes each watched file's size and its millisecond
        // mtime, so a same-size rewrite inside the same millisecond hashes
        // identically and the loader commits nothing.
        const QString fragPath = packs.path() + QStringLiteral("/reload-glow/effect.frag");
        {
            QFile tmpFrag(fragPath + QStringLiteral(".new"));
            QVERIFY(tmpFrag.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(tmpFrag.write(QByteArrayLiteral("// edited in place by the test\n"
                                                    "vec4 pSurface(vec2 uv)\n"
                                                    "{\n"
                                                    "    return vec4(0.0, float(p_glowSize), 0.0, 1.0);\n"
                                                    "}\n"))
                    > 0);
            tmpFrag.close();
            QVERIFY(QFile::remove(fragPath));
            QVERIFY(QFile::rename(fragPath + QStringLiteral(".new"), fragPath));
        }
        QVERIFY2(reloads.wait(15000), "a live pack-source edit must bump the reload generation");
        QVERIFY(chrome.decorationReloadGeneration() > 0);
    }

    void decorationComponentIsPlainStorage()
    {
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QSignalSpy spy(&chrome, &ShellChrome::decorationComponentChanged);
        QObject component;
        chrome.setDecorationComponent(&component);
        chrome.setDecorationComponent(&component);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(chrome.decorationComponent(), &component);
    }
};

QTEST_GUILESS_MAIN(TestShellChrome)

#include "test_shell_chrome.moc"
