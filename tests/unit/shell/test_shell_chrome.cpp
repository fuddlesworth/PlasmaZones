// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ShellChrome turns the decoration tree the daemon publishes into the
// stage list SurfaceDecoration draws, one stage per installed pack in a
// surface's enabled chain, against the bundled packs in data/surface.

#include "shell/ShellChrome.h"

#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <QDir>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorShellApp;
using namespace PhosphorSurfaceShaders;

namespace {

QString treeJson(const QString& path, const QStringList& chain, const QVariantMap& params = {})
{
    DecorationProfileTree tree;
    DecorationProfile profile;
    profile.chain = chain;
    if (!params.isEmpty()) {
        profile.parameters = params;
    }
    tree.setOverride(path, profile);
    return QString::fromUtf8(QJsonDocument(tree.toJson()).toJson(QJsonDocument::Compact));
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

    void outerPaddingIsClampedToTheCeiling()
    {
        // A typo'd or hostile parameter cannot demand an unbounded canvas.
        ShellChrome chrome({QStringLiteral(PZ_BUNDLED_SURFACE_DIR)}, nullptr);
        QVariantMap params;
        QVariantMap glow;
        glow.insert(QStringLiteral("glowSize"), 100000);
        params.insert(QStringLiteral("glow"), glow);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, params)));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()),
                 static_cast<double>(PhosphorSurfaceShaders::kMaxDecorationOuterPaddingPx));

        // And a negative request floors at zero rather than shrinking the surface.
        QVariantMap negative;
        QVariantMap shrink;
        shrink.insert(QStringLiteral("glowSize"), -60);
        negative.insert(QStringLiteral("glow"), shrink);
        QVERIFY(chrome.setTreeJson(treeJson(decorationShellPhosphorOsdPath(), {QStringLiteral("glow")}, negative)));
        QCOMPARE(chrome.outerPaddingFor(decorationShellPhosphorOsdPath()), 0.0);
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
