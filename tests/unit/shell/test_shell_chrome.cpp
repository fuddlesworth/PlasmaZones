// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ShellChrome turns the decoration tree the daemon publishes into the
// stage list SurfaceDecoration draws, one stage per installed pack in a
// surface's enabled chain, against the bundled packs in data/surface.

#include "shell/ShellChrome.h"

#include <PhosphorShaders/ShaderPresetStore.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
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

        // And a negative request floors at zero rather than shrinking the surface.
        // Clamped to the declared MIN of 4 on the way through, then the padding
        // request's own floor; either way the surface never loses room.
        QVariantMap negative;
        QVariantMap shrink;
        shrink.insert(QStringLiteral("glowSize"), -60);
        negative.insert(QStringLiteral("glow"), shrink);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, negative)));
        QVERIFY(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()) >= 0.0);
    }

    void malformedJsonKeepsTheTree()
    {
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorBarPath(), {QStringLiteral("border-phosphor")})));
        QVERIFY(!chrome.setTreeJson(QStringLiteral("[not an object")));
        QCOMPARE(chrome.chainFor(decorationShellPhosphorBarPath()).size(), 1);
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
