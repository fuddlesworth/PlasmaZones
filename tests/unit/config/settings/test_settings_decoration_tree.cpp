// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_decoration_tree.cpp
 * @brief Settings — DecorationProfileTree persistence + JSON facade.
 *
 * The decoration analogue of test_settings_shader_tree.cpp. The
 * per-surface decoration tree persists as one nested JSON entry under
 * Decorations/DecorationProfileTree, mirroring how the animation
 * shaderProfileTree persists under Animations/ShaderProfileTree. Pinned
 * behaviour:
 *   - a fresh Settings returns the ConfigDefaults tree: a neutral baseline
 *     (no global chain — border/titlebar visuals are rule-owned) plus the
 *     built-in card chrome for the OSD and PopupFrame popups
 *   - the card chrome is a read-side SEED layer, not stored data: a blob
 *     that lacks the seeded paths still renders them, an explicit empty
 *     chain keeps a surface undecorated, and the write path strips
 *     seed-identical overrides so the stored blob holds only user edits
 *   - setDecorationProfileTree round-trips a baseline chain + a leaf
 *     override across save/load and across a fresh Settings instance
 *     (the cross-process daemon path)
 *   - the setter's value-equality gate emits decorationProfileTreeChanged
 *     exactly once and stays silent on a same-tree write
 *   - the decorationProfileTreeJson facade returns compact JSON;
 *     setDecorationProfileTreeJson("") resets to the empty default and
 *     malformed JSON is ignored (no signal, tree unchanged)
 */

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <PhosphorSurface/DecorationSupportedPaths.h>

#include <QTest>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include "config/configbackends.h"
#include "config/configdefaults.h"
#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// A baseline-plus-leaf tree used by several tests: baseline decorates
/// every window with the border+glow chain, while `window.tiled` narrows
/// to just glow.
PhosphorSurfaceShaders::DecorationProfileTree makeBaselinePlusLeafTree()
{
    PhosphorSurfaceShaders::DecorationProfileTree tree;

    PhosphorSurfaceShaders::DecorationProfile baseline;
    baseline.chain = QStringList{QStringLiteral("border"), QStringLiteral("glow")};
    tree.setBaseline(baseline);

    PhosphorSurfaceShaders::DecorationProfile leaf;
    leaf.chain = QStringList{QStringLiteral("glow")};
    tree.setOverride(QStringLiteral("window.tiled"), leaf);

    return tree;
}

/// The Phosphor shell's own surfaces live under the baseline-isolated
/// `shell.*` root, so their seeds are injected even when the user engaged a
/// global baseline chain (that is the isolation's purpose: a window chain
/// must never veto the chrome's defaults). Every read of the settings tree
/// therefore carries them; a user tree compares equal to its read-back only
/// once it wears the same seeds.
PhosphorSurfaceShaders::DecorationProfileTree withShellSeeds(const PhosphorSurfaceShaders::DecorationProfileTree& tree)
{
    return tree.withSeedDefaults(ConfigDefaults::decorationProfileTree());
}

} // namespace

class TestSettingsDecorationTree : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// A fresh config has no Decorations/DecorationProfileTree entry, so
    /// Settings::decorationProfileTree() must fall back to the canonical
    /// ConfigDefaults default. The baseline stays neutral (no global chain —
    /// window/popup border and titlebar visuals are owned by the window
    /// rules), but the PopupFrame card surfaces — "osd",
    /// "popup.layoutPicker", "popup.zoneSelector", "popup.cheatsheet" —
    /// each ship the same default
    /// decoration chain (a crisp neutral frame-contrast border + a real,
    /// theme-tinted drop shadow) that chromes their cards through the
    /// surface-decoration pipeline. Snap-assist carries its own anchor, not
    /// PopupFrame, so it is left undecorated.
    void testDecorationProfileTree_defaultShipsCardChains()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        const auto tree = settings.decorationProfileTree();
        QCOMPARE(tree, ConfigDefaults::decorationProfileTree());
        QVERIFY2(!tree.baseline().chain.has_value(), "default baseline must carry no chain (fully neutral)");
        QStringList expectedPaths{QStringLiteral("osd"), QStringLiteral("popup.layoutPicker"),
                                  QStringLiteral("popup.zoneSelector"), QStringLiteral("popup.cheatsheet")};
#ifdef PLASMAZONES_HAVE_PHOSPHOR_SHELL
        // The Phosphor shell's chrome seeds ride behind the shell gate.
        expectedPaths += PhosphorSurfaceShaders::decorationShellPhosphorLeafPaths();
#endif
        QCOMPARE(tree.overriddenPaths(), expectedPaths);

        // Every card surface resolves to the same border + theme-tinted shadow.
        const QStringList cardSurfaces{QStringLiteral("osd"), QStringLiteral("popup.layoutPicker"),
                                       QStringLiteral("popup.zoneSelector"), QStringLiteral("popup.cheatsheet")};
        for (const QString& path : cardSurfaces) {
            const auto card = tree.resolve(path);
            QCOMPARE(card.enabledChain(), (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));
            const auto borderParams = card.effectiveParameters().value(QStringLiteral("border")).toMap();
            QVERIFY2(borderParams.value(QStringLiteral("useThemeNeutral")).toBool(),
                     qPrintable(QStringLiteral("%1 border must derive a neutral theme colour").arg(path)));
            QCOMPARE(borderParams.value(QStringLiteral("borderWidth")).toInt(), 1);
            const auto shadowParams = card.effectiveParameters().value(QStringLiteral("shadow")).toMap();
            QVERIFY2(shadowParams.value(QStringLiteral("useThemeTint")).toBool(),
                     qPrintable(QStringLiteral("%1 shadow must tint with the theme background").arg(path)));
        }

        // Snap-assist stays undecorated (no PopupFrame chrome to replace).
        QVERIFY2(tree.resolve(QStringLiteral("popup.snapAssist")).enabledChain().isEmpty(),
                 "snap-assist must not ship a default decoration");
    }

    /// The tree — a baseline chain plus a leaf override — must survive
    /// save() and reload through a fresh Settings instance. This is the
    /// cross-process path: the daemon's Settings reads the same disk file
    /// the settings app wrote. Regression guard mirroring the shader-tree
    /// round-trip (a missing Decorations-schema key would drop the blob at
    /// the Store gate and silently reset the picker).
    void testDecorationProfileTree_setRoundTripsThroughDisk()
    {
        IsolatedConfigGuard guard;

        // Write through Settings A; verify the round-trip in the SAME
        // instance first.
        {
            Settings a;
            QSignalSpy spy(&a, &Settings::decorationProfileTreeChanged);
            a.setDecorationProfileTree(makeBaselinePlusLeafTree());
            QCOMPARE(spy.count(), 1);
            a.save();

            const auto reread = a.decorationProfileTree();
            QCOMPARE(reread.baseline().effectiveChain(),
                     (QStringList{QStringLiteral("border"), QStringLiteral("glow")}));
            QVERIFY(reread.hasOverride(QStringLiteral("window.tiled")));
            QCOMPARE(reread.directOverride(QStringLiteral("window.tiled")).effectiveChain(),
                     QStringList{QStringLiteral("glow")});
        }

        // Open a fresh Settings on the same isolated config — the value
        // must survive the file load.
        {
            Settings b;
            const auto reread = b.decorationProfileTree();
            QVERIFY2(reread.hasOverride(QStringLiteral("window.tiled")),
                     "DecorationProfileTree must persist across Settings instances");
            QCOMPARE(reread.baseline().effectiveChain(),
                     (QStringList{QStringLiteral("border"), QStringLiteral("glow")}));
            QCOMPARE(reread.directOverride(QStringLiteral("window.tiled")).effectiveChain(),
                     QStringList{QStringLiteral("glow")});
        }
    }

    /// The setter's value-equality gate: a real change fires
    /// decorationProfileTreeChanged (and settingsChanged) exactly once,
    /// and writing the identical tree again is a no-op that fires
    /// nothing. Without the gate the QML two-way binding would re-dirty
    /// the page on every refresh.
    void testDecorationProfileTree_setterSignalsOnceAndGatesEqual()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QSignalSpy specificSpy(&settings, &Settings::decorationProfileTreeChanged);
        QSignalSpy generalSpy(&settings, &Settings::settingsChanged);
        QVERIFY(specificSpy.isValid());
        QVERIFY(generalSpy.isValid());

        const auto tree = makeBaselinePlusLeafTree();
        settings.setDecorationProfileTree(tree);
        QCOMPARE(specificSpy.count(), 1);
        QVERIFY(generalSpy.count() >= 1);

        // Same tree again — value-equality gate must suppress the emit.
        settings.setDecorationProfileTree(tree);
        QCOMPARE(specificSpy.count(), 1);
    }

    /// decorationProfileTreeJson returns a compact serialization of the
    /// current tree that round-trips back through
    /// DecorationProfileTree::fromJson to an equal tree — this is the
    /// blob the QML two-way binding reads.
    void testDecorationProfileTreeJson_returnsCompactRoundTrippableJson()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        const auto tree = makeBaselinePlusLeafTree();
        settings.setDecorationProfileTree(tree);

        const QString json = settings.decorationProfileTreeJson();
        QVERIFY(!json.isEmpty());
        // Compact JSON carries no newline (indented JSON would).
        QVERIFY2(!json.contains(QLatin1Char('\n')), "decorationProfileTreeJson must be compact");

        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isObject());
        const auto parsed = PhosphorSurfaceShaders::DecorationProfileTree::fromJson(doc.object());
        QCOMPARE(parsed, withShellSeeds(tree));
    }

    /// setDecorationProfileTreeJson("") resets to the canonical default
    /// (the EMPTY/neutral tree), exactly like the animation
    /// shaderProfileTree facade. Clearing restores "no decoration".
    void testDecorationProfileTreeJson_emptyStringResetsToDefault()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        settings.setDecorationProfileTree(makeBaselinePlusLeafTree());
        QVERIFY(settings.decorationProfileTree().hasOverride(QStringLiteral("window.tiled")));

        settings.setDecorationProfileTreeJson(QString());
        QCOMPARE(settings.decorationProfileTree(), ConfigDefaults::decorationProfileTree());
        QVERIFY2(!settings.decorationProfileTree().hasOverride(QStringLiteral("window.tiled")),
                 "empty-string reset must drop every override");
    }

    /// Malformed JSON is ignored: the setter neither mutates the tree nor
    /// fires the changed signal, so a bad two-way-binding write can't
    /// clobber the user's decoration.
    void testDecorationProfileTreeJson_malformedIsIgnored()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        const auto tree = makeBaselinePlusLeafTree();
        settings.setDecorationProfileTree(tree);

        QSignalSpy spy(&settings, &Settings::decorationProfileTreeChanged);
        settings.setDecorationProfileTreeJson(QStringLiteral("{ this is not valid json"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(settings.decorationProfileTree(), withShellSeeds(tree));
    }

    /// committedDecorationProfileTree() is the baseline the per-surface
    /// decoration dirty check and scoped Discard compare against. It must track
    /// the last load/save commit, NOT live edits — setDecorationProfileTree
    /// persists to the store immediately but does not re-baseline, so live and
    /// committed diverge until save(). A regression here silently breaks
    /// per-page dirty/Discard for the decoration surface pages.
    void testCommittedDecorationProfileTree_tracksBaselineNotLiveEdits()
    {
        IsolatedConfigGuard guard;
        Settings a;

        // Fresh load: committed baseline equals the live tree (the ConfigDefaults
        // default), and carries no window.tiled override.
        QVERIFY(a.committedDecorationProfileTree() == a.decorationProfileTree());
        QVERIFY(!a.committedDecorationProfileTree().hasOverride(QStringLiteral("window.tiled")));

        // Edit without save: live gains the window.tiled leaf; committed holds.
        a.setDecorationProfileTree(makeBaselinePlusLeafTree());
        QVERIFY(a.decorationProfileTree().hasOverride(QStringLiteral("window.tiled")));
        QVERIFY(!a.committedDecorationProfileTree().hasOverride(QStringLiteral("window.tiled")));
        QVERIFY(a.decorationProfileTree() != a.committedDecorationProfileTree());

        // save() re-captures the baseline: committed catches up to live.
        a.save();
        QVERIFY(a.committedDecorationProfileTree().hasOverride(QStringLiteral("window.tiled")));
        QVERIFY(a.committedDecorationProfileTree() == a.decorationProfileTree());
    }

    /// The card chrome is a read-side seed layer, not stored data: a persisted
    /// blob that never mentions the seeded paths (a user edit elsewhere in the
    /// tree, or a config written before the defaults existed) still renders
    /// the OSD/popup defaults. Regression guard for the "boring square
    /// surfaces" failure where any saved tree silently dropped the chrome.
    void testDecorationProfileTree_seedsSurviveUnrelatedUserEdits()
    {
        IsolatedConfigGuard guard;

        Settings a;
        // An unrelated edit: window.tiled gets glow, nothing baseline, nothing
        // at the seeded paths.
        PhosphorSurfaceShaders::DecorationProfileTree tree = a.decorationProfileTree();
        PhosphorSurfaceShaders::DecorationProfile leaf;
        leaf.chain = QStringList{QStringLiteral("glow")};
        tree.setOverride(QStringLiteral("window.tiled"), leaf);
        a.setDecorationProfileTree(tree);
        a.save();

        // Same instance and a fresh instance (the daemon's cross-process read)
        // both keep the seeded chrome next to the user edit.
        const auto reread = a.decorationProfileTree();
        QCOMPARE(reread.directOverride(QStringLiteral("window.tiled")).effectiveChain(),
                 QStringList{QStringLiteral("glow")});
        QCOMPARE(reread.resolve(QStringLiteral("osd")).enabledChain(),
                 (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));
        Settings b;
        QCOMPARE(b.decorationProfileTree().resolve(QStringLiteral("osd")).enabledChain(),
                 (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));
    }

    /// The write path strips overrides the seed layer regenerates verbatim, so
    /// the stored blob holds only user edits — a shipped default improvement
    /// must reach configs that never customized the seeded surfaces. Callers
    /// read the MERGED tree and write the whole tree back, so without the
    /// strip the injected chrome would freeze into the blob on the first
    /// unrelated edit.
    void testDecorationProfileTree_writeStripsSeedIdenticalOverrides()
    {
        IsolatedConfigGuard guard;

        Settings a;
        // Write the merged view back with one real edit — the seed entries ride
        // along exactly as injected.
        PhosphorSurfaceShaders::DecorationProfileTree tree = a.decorationProfileTree();
        QVERIFY(tree.hasOverride(QStringLiteral("osd")));
        PhosphorSurfaceShaders::DecorationProfile leaf;
        leaf.chain = QStringList{QStringLiteral("glow")};
        tree.setOverride(QStringLiteral("window.tiled"), leaf);
        a.setDecorationProfileTree(tree);
        a.save();

        // The persisted blob must carry ONLY the user edit.
        QFile file(guard.configPath() + QStringLiteral("/plasmazones/config.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject blob = root.value(QLatin1String("Decorations"))
                                     .toObject()
                                     .value(QLatin1String("DecorationProfileTree"))
                                     .toObject();
        const auto stored = PhosphorSurfaceShaders::DecorationProfileTree::fromJson(blob);
        QCOMPARE(stored.overriddenPaths(), QStringList{QStringLiteral("window.tiled")});
    }

    /// Clearing a seeded surface's user override (what per-page Reset does)
    /// reveals the seed chrome again, and an explicit empty chain persists as
    /// "undecorated" across instances. Regression guard for the Reset flow
    /// that left the OSD bare instead of restoring the defaults.
    void testDecorationProfileTree_resetRevealsSeedsAndExplicitEmptySticks()
    {
        IsolatedConfigGuard guard;

        Settings a;
        // Customize the OSD, then clear the override (Reset).
        PhosphorSurfaceShaders::DecorationProfileTree tree = a.decorationProfileTree();
        PhosphorSurfaceShaders::DecorationProfile custom;
        custom.chain = QStringList{QStringLiteral("glow")};
        tree.setOverride(QStringLiteral("osd"), custom);
        a.setDecorationProfileTree(tree);
        QCOMPARE(a.decorationProfileTree().resolve(QStringLiteral("osd")).enabledChain(),
                 QStringList{QStringLiteral("glow")});

        tree = a.decorationProfileTree();
        QVERIFY(tree.clearOverride(QStringLiteral("osd")));
        a.setDecorationProfileTree(tree);
        QCOMPARE(a.decorationProfileTree().resolve(QStringLiteral("osd")).enabledChain(),
                 (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));

        // Explicitly undecorated: engaged-but-empty chain survives save + a
        // fresh instance without the seed resurrecting.
        tree = a.decorationProfileTree();
        PhosphorSurfaceShaders::DecorationProfile none;
        none.chain = QStringList{};
        tree.setOverride(QStringLiteral("osd"), none);
        a.setDecorationProfileTree(tree);
        a.save();
        QVERIFY(a.decorationProfileTree().resolve(QStringLiteral("osd")).enabledChain().isEmpty());
        Settings b;
        QVERIFY2(b.decorationProfileTree().resolve(QStringLiteral("osd")).enabledChain().isEmpty(),
                 "an explicit empty chain must persist as undecorated");
    }

    /// A parameters-only retune of a seeded surface persists ONLY the
    /// parameters: the UI writes back the merged view (seed chain injected),
    /// and the field-level strip must drop the seed-equal chain so it stays
    /// seed-owned (a shipped chain improvement still reaches this config)
    /// while the retuned map survives and resolves under the seed chain.
    void testDecorationProfileTree_paramsOnlyRetuneKeepsChainSeedOwned()
    {
        IsolatedConfigGuard guard;

        Settings a;
        // Retune exactly as the UI does: read the merged tree, tweak one
        // param on the seeded osd override, write the whole tree back.
        PhosphorSurfaceShaders::DecorationProfileTree tree = a.decorationProfileTree();
        PhosphorSurfaceShaders::DecorationProfile osd = tree.directOverride(QStringLiteral("osd"));
        QVERIFY(osd.chain.has_value());
        QVariantMap params = osd.effectiveParameters();
        QVariantMap shadowParams = params.value(QStringLiteral("shadow")).toMap();
        shadowParams.insert(QStringLiteral("shadowStrength"), 0.95);
        params.insert(QStringLiteral("shadow"), shadowParams);
        osd.parameters = params;
        tree.setOverride(QStringLiteral("osd"), osd);
        a.setDecorationProfileTree(tree);
        a.save();

        // Stored blob: parameters engaged, chain NOT engaged (seed-owned).
        QFile file(guard.configPath() + QStringLiteral("/plasmazones/config.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const auto stored =
            PhosphorSurfaceShaders::DecorationProfileTree::fromJson(root.value(QLatin1String("Decorations"))
                                                                        .toObject()
                                                                        .value(QLatin1String("DecorationProfileTree"))
                                                                        .toObject());
        QVERIFY(stored.hasOverride(QStringLiteral("osd")));
        QVERIFY2(!stored.directOverride(QStringLiteral("osd")).chain.has_value(),
                 "a params-only retune must not freeze the seed chain into the blob");
        QVERIFY(stored.directOverride(QStringLiteral("osd")).parameters.has_value());

        // A fresh instance resolves the seed chain with the retuned map.
        Settings b;
        const auto resolved = b.decorationProfileTree().resolve(QStringLiteral("osd"));
        QCOMPARE(resolved.enabledChain(), (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));
        QCOMPARE(resolved.effectiveParameters()
                     .value(QStringLiteral("shadow"))
                     .toMap()
                     .value(QStringLiteral("shadowStrength"))
                     .toDouble(),
                 0.95);
    }

    /// The strip's whole-view guard must REFUSE a strip that would change the
    /// resolved result: a chain-only override equal to the seed chain blocks
    /// the seed's parameters via the master gate, so stripping it would let
    /// the seed parameters inject. The override must persist as-is, and the
    /// merged view must keep the osd free of seed parameters.
    void testDecorationProfileTree_chainOnlySeedEqualOverrideIsNotStripped()
    {
        IsolatedConfigGuard guard;

        Settings a;
        const auto seedChain =
            ConfigDefaults::decorationProfileTree().directOverride(QStringLiteral("osd")).effectiveChain();
        PhosphorSurfaceShaders::DecorationProfileTree tree = a.decorationProfileTree();
        PhosphorSurfaceShaders::DecorationProfile chainOnly;
        chainOnly.chain = seedChain;
        tree.setOverride(QStringLiteral("osd"), chainOnly);
        a.setDecorationProfileTree(tree);
        a.save();

        // Stored blob keeps the chain-engaged override.
        QFile file(guard.configPath() + QStringLiteral("/plasmazones/config.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const auto stored =
            PhosphorSurfaceShaders::DecorationProfileTree::fromJson(root.value(QLatin1String("Decorations"))
                                                                        .toObject()
                                                                        .value(QLatin1String("DecorationProfileTree"))
                                                                        .toObject());
        QVERIFY2(stored.hasOverride(QStringLiteral("osd"))
                     && stored.directOverride(QStringLiteral("osd")).chain.has_value(),
                 "a chain-only override must not be stripped even when it equals the seed chain");

        // Merged view: the engaged chain closes the master gate, so the seed
        // parameters must NOT appear at osd.
        const auto merged = a.decorationProfileTree();
        QCOMPARE(merged.resolve(QStringLiteral("osd")).enabledChain(), seedChain);
        QVERIFY2(!merged.directOverride(QStringLiteral("osd")).parameters.has_value(),
                 "seed parameters must not inject under a user-engaged chain");
    }

    /// Cross-root isolation of the per-page Reset mechanism: clearing the
    /// "osd" root's overrides from the merged view (what
    /// SettingsController::resetPage does for the OSDs page) reveals the osd
    /// seed again while a real popup edit stands, and the stored blob keeps
    /// only that edit.
    void testDecorationProfileTree_rootScopedClearPreservesSiblingRoots()
    {
        IsolatedConfigGuard guard;

        Settings a;
        // A real edit in the popup root plus a customized osd.
        PhosphorSurfaceShaders::DecorationProfileTree tree = a.decorationProfileTree();
        PhosphorSurfaceShaders::DecorationProfile glow;
        glow.chain = QStringList{QStringLiteral("glow")};
        tree.setOverride(QStringLiteral("popup.zoneSelector"), glow);
        tree.setOverride(QStringLiteral("osd"), glow);
        a.setDecorationProfileTree(tree);

        // resetPage("decorations-osds") mechanism: clear this root's overrides
        // from the merged view and write back.
        tree = a.decorationProfileTree();
        const QStringList paths = tree.overriddenPaths();
        for (const QString& path : paths) {
            if (path == QLatin1String("osd") || path.startsWith(QLatin1String("osd.")))
                tree.clearOverride(path);
        }
        a.setDecorationProfileTree(tree);
        a.save();

        // OSD seed chrome is back; the popup edit stands.
        const auto merged = a.decorationProfileTree();
        QCOMPARE(merged.resolve(QStringLiteral("osd")).enabledChain(),
                 (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));
        QCOMPARE(merged.resolve(QStringLiteral("popup.zoneSelector")).enabledChain(),
                 QStringList{QStringLiteral("glow")});

        // Stored blob holds only the popup edit.
        QFile file(guard.configPath() + QStringLiteral("/plasmazones/config.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const auto stored =
            PhosphorSurfaceShaders::DecorationProfileTree::fromJson(root.value(QLatin1String("Decorations"))
                                                                        .toObject()
                                                                        .value(QLatin1String("DecorationProfileTree"))
                                                                        .toObject());
        QCOMPARE(stored.overriddenPaths(), QStringList{QStringLiteral("popup.zoneSelector")});
    }

    /// The pointer surface is the fifth root the tree carries, and it rides
    /// the same contract as the four window/osd/popup/shell roots: an edit
    /// there round-trips through the store, shows as a live-vs-committed
    /// diff until save(), survives another root's scoped reset, and is
    /// removed by its own.
    void testDecorationProfileTree_pointerRootRoundTripsAndResetsInItsOwnScope()
    {
        IsolatedConfigGuard guard;
        const QString pointer = PhosphorSurfaceShaders::decorationPointerPath();

        Settings a;
        PhosphorSurfaceShaders::DecorationProfileTree tree = a.decorationProfileTree();
        PhosphorSurfaceShaders::DecorationProfile halo;
        halo.chain = QStringList{QStringLiteral("halo")};
        tree.setOverride(pointer, halo);
        PhosphorSurfaceShaders::DecorationProfile glow;
        glow.chain = QStringList{QStringLiteral("glow")};
        tree.setOverride(QStringLiteral("osd"), glow);
        a.setDecorationProfileTree(tree);

        // Live carries the pointer edit; committed does not until save().
        QCOMPARE(a.decorationProfileTree().resolve(pointer).enabledChain(), QStringList{QStringLiteral("halo")});
        QVERIFY(!a.committedDecorationProfileTree().hasOverride(pointer));
        a.save();
        QVERIFY(a.committedDecorationProfileTree().hasOverride(pointer));
        Settings b;
        QCOMPARE(b.decorationProfileTree().resolve(pointer).enabledChain(), QStringList{QStringLiteral("halo")});

        // Another root's scoped reset (the OSDs page) leaves the pointer edit.
        tree = a.decorationProfileTree();
        for (const QString& path : tree.overriddenPaths()) {
            if (path == QLatin1String("osd") || path.startsWith(QLatin1String("osd.")))
                tree.clearOverride(path);
        }
        a.setDecorationProfileTree(tree);
        QVERIFY(a.decorationProfileTree().hasOverride(pointer));

        // The pointer page's own reset removes it, and the store holds nothing
        // for it afterwards.
        tree = a.decorationProfileTree();
        for (const QString& path : tree.overriddenPaths()) {
            if (path == pointer || path.startsWith(pointer + QLatin1Char('.')))
                tree.clearOverride(path);
        }
        a.setDecorationProfileTree(tree);
        a.save();
        QVERIFY(!a.decorationProfileTree().hasOverride(pointer));
        QVERIFY(a.decorationProfileTree().resolve(pointer).enabledChain().isEmpty());
        QFile file(guard.configPath() + QStringLiteral("/plasmazones/config.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const auto stored =
            PhosphorSurfaceShaders::DecorationProfileTree::fromJson(root.value(QLatin1String("Decorations"))
                                                                        .toObject()
                                                                        .value(QLatin1String("DecorationProfileTree"))
                                                                        .toObject());
        QVERIFY(!stored.hasOverride(pointer));
    }

    /// The write side bounds every profile's size, whichever door it came in
    /// by: a chain past 64 entries, a parameter map past 64 keys, and any
    /// string past 1024 chars are trimmed rather than stored.
    void testDecorationProfileTree_writeBoundsProfileSizes()
    {
        IsolatedConfigGuard guard;

        Settings a;
        PhosphorSurfaceShaders::DecorationProfileTree tree = a.decorationProfileTree();
        PhosphorSurfaceShaders::DecorationProfile huge;
        QStringList chain;
        for (int i = 0; i < 80; ++i)
            chain.append(QStringLiteral("pack%1").arg(i));
        chain.append(QString(2000, QLatin1Char('x')));
        huge.chain = chain;
        QVariantMap packParams;
        for (int i = 0; i < 80; ++i)
            packParams.insert(QStringLiteral("p%1").arg(i), i);
        packParams.insert(QStringLiteral("long"), QString(2000, QLatin1Char('y')));
        huge.parameters = QVariantMap{{QStringLiteral("pack0"), packParams}};
        tree.setOverride(QStringLiteral("window.tiled"), huge);
        a.setDecorationProfileTree(tree);

        const auto stored = a.decorationProfileTree().directOverride(QStringLiteral("window.tiled"));
        QVERIFY(stored.chain.has_value());
        QCOMPARE(stored.chain->size(), 64);
        QVERIFY(!stored.chain->contains(QString(2000, QLatin1Char('x'))));
        QVERIFY(stored.parameters.has_value());
        const QVariantMap storedPack = stored.parameters->value(QStringLiteral("pack0")).toMap();
        // EXACTLY the cap, like the chain assertion two lines above.
        QCOMPARE(storedPack.size(), 64);
        QVERIFY(!storedPack.contains(QStringLiteral("long")));
    }

    /// Write `tree` straight to the backend, bypassing the setter, so the next
    /// `Settings` reads exactly these bytes.
    ///
    /// Which is the only way to reach the READ path: the setter bounds on the
    /// way in, so a tree written through it can never be the hand-edited
    /// `config.json` the schema sanitizer exists for.
    void writeRawDecorationTree(const PhosphorSurfaceShaders::DecorationProfileTree& tree)
    {
        auto backend = PlasmaZones::createDefaultConfigBackend();
        auto decorations = backend->group(ConfigDefaults::decorationsGroup());
        decorations->writeJson(ConfigDefaults::decorationProfileTreeKey(), tree.toJson());
    }

    /// The decoration sanitizer on the READ path, which is the half the setter
    /// cannot cover.
    ///
    /// The sanitizer was registered on the KeyDef with nothing driving it:
    /// removing it left every test in this file passing, because they all write
    /// through the setter and the setter has its own bound. A hand-edited
    /// `config.json` goes through neither.
    void testDecorationTreeSanitizerBoundsAHandEditedBlob()
    {
        IsolatedConfigGuard guard;
        const QString kPath = QStringLiteral("window.tiled");

        PhosphorSurfaceShaders::DecorationProfileTree tree;
        PhosphorSurfaceShaders::DecorationProfile huge;
        QStringList chain;
        for (int i = 0; i < 80; ++i)
            chain.append(QStringLiteral("pack%1").arg(i));
        huge.chain = chain;
        QVariantMap packParams;
        for (int i = 0; i < 80; ++i)
            packParams.insert(QStringLiteral("p%1").arg(i), i);
        packParams.insert(QStringLiteral("long"), QString(2000, QLatin1Char('y')));
        huge.parameters = QVariantMap{{QStringLiteral("pack0"), packParams},
                                      // A scalar where a pack's parameter map
                                      // belongs resolves to no parameters at
                                      // all, so keeping it preserves nothing.
                                      {QStringLiteral("pack1"), 7}};
        huge.presetIds = QVariantMap{{QStringLiteral("pack0"), QStringLiteral("a1b2c3")},
                                     {QStringLiteral("pack1"), QString(2000, QLatin1Char('z'))},
                                     {QStringLiteral("pack2"), 7}};
        tree.setOverride(kPath, huge);
        writeRawDecorationTree(tree);

        Settings a;
        const auto stored = a.decorationProfileTree().directOverride(kPath);
        QVERIFY(stored.chain.has_value());
        QCOMPARE(stored.chain->size(), 64);
        QVERIFY(stored.parameters.has_value());
        QVERIFY(!stored.parameters->contains(QStringLiteral("pack1")));
        QCOMPARE(stored.parameters->value(QStringLiteral("pack0")).toMap().size(), 64);
        QVERIFY(!stored.parameters->value(QStringLiteral("pack0")).toMap().contains(QStringLiteral("long")));
        // presetIds is flat and string-valued at both levels, so an over-long
        // or non-string value goes while the usable entry stays.
        QVERIFY(stored.presetIds.has_value());
        QCOMPARE(stored.presetIds->value(QStringLiteral("pack0")).toString(), QStringLiteral("a1b2c3"));
        QVERIFY(!stored.presetIds->contains(QStringLiteral("pack1")));
        QVERIFY(!stored.presetIds->contains(QStringLiteral("pack2")));
    }

    /// A duplicate chain entry is dropped, which the setter does not judge.
    ///
    /// The compositor folds the chain per entry, so a repeated pack id costs a
    /// draw and a buffer slot for nothing.
    void testDecorationTreeSanitizerDropsDuplicateChainEntries()
    {
        IsolatedConfigGuard guard;
        const QString kPath = QStringLiteral("window.tiled");

        PhosphorSurfaceShaders::DecorationProfileTree tree;
        PhosphorSurfaceShaders::DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border"), QStringLiteral("glow"), QStringLiteral("border")};
        tree.setOverride(kPath, p);
        writeRawDecorationTree(tree);

        Settings a;
        const auto stored = a.decorationProfileTree().directOverride(kPath);
        QVERIFY(stored.chain.has_value());
        QCOMPARE(*stored.chain, QStringList({QStringLiteral("border"), QStringLiteral("glow")}));
    }

    /// The sanitizer leaves an ordinary blob alone, presets included.
    ///
    /// The negative control for the two above: a bound that rewrote a
    /// legitimate tree on every read would be worse than no bound, because the
    /// damage would be invisible until the user looked.
    void testDecorationTreeSanitizerLeavesAnOrdinaryBlobAlone()
    {
        IsolatedConfigGuard guard;
        const QString kPath = QStringLiteral("window.tiled");

        PhosphorSurfaceShaders::DecorationProfileTree tree;
        PhosphorSurfaceShaders::DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border"), QStringLiteral("glow")};
        p.disabledPacks = QStringList{QStringLiteral("shadow")};
        p.presetIds = QVariantMap{{QStringLiteral("border"), QStringLiteral("a1b2c3")}};
        p.parameters = QVariantMap{{QStringLiteral("border"), QVariantMap{{QStringLiteral("width"), 2}}}};
        tree.setOverride(kPath, p);
        writeRawDecorationTree(tree);

        Settings a;
        const auto stored = a.decorationProfileTree().directOverride(kPath);
        QCOMPARE(*stored.chain, QStringList({QStringLiteral("border"), QStringLiteral("glow")}));
        QCOMPARE(*stored.disabledPacks, QStringList({QStringLiteral("shadow")}));
        QCOMPARE(stored.presetIdFor(QStringLiteral("border")), QStringLiteral("a1b2c3"));
        QCOMPARE(stored.parameters->value(QStringLiteral("border")).toMap().value(QStringLiteral("width")).toInt(), 2);
    }

    /// An engaged-but-empty field survives the bound.
    ///
    /// Engaged-empty is "explicitly undecorated" / "explicitly no preset",
    /// which is how a child stops inheriting an ancestor's. A bound that
    /// disengaged the field would silently turn that back into "inherit".
    void testDecorationTreeSanitizerPreservesEngagedEmptyFields()
    {
        IsolatedConfigGuard guard;
        const QString kPath = QStringLiteral("window.tiled");

        PhosphorSurfaceShaders::DecorationProfileTree tree;
        PhosphorSurfaceShaders::DecorationProfile p;
        p.chain = QStringList{};
        p.presetIds = QVariantMap{};
        tree.setOverride(kPath, p);
        writeRawDecorationTree(tree);

        Settings a;
        const auto stored = a.decorationProfileTree().directOverride(kPath);
        QVERIFY(stored.chain.has_value());
        QVERIFY(stored.chain->isEmpty());
        QVERIFY(stored.presetIds.has_value());
        QVERIFY(stored.presetIds->isEmpty());
    }

    /// The SETTER bounds presetIds too.
    ///
    /// It did not: every other field had a setter-side bound and this one was
    /// covered by the schema alone, so which bound applied came down to which
    /// door the write came in by.
    void testDecorationProfileTree_writeBoundsPresetIds()
    {
        IsolatedConfigGuard guard;

        Settings a;
        PhosphorSurfaceShaders::DecorationProfileTree tree = a.decorationProfileTree();
        PhosphorSurfaceShaders::DecorationProfile p;
        p.chain = QStringList{QStringLiteral("border")};
        QVariantMap presets{{QStringLiteral("border"), QStringLiteral("a1b2c3")},
                            {QStringLiteral("glow"), QString(2000, QLatin1Char('z'))},
                            {QStringLiteral("shadow"), 7}};
        for (int i = 0; i < 80; ++i)
            presets.insert(QStringLiteral("pack%1").arg(i), QStringLiteral("p%1").arg(i));
        p.presetIds = presets;
        tree.setOverride(QStringLiteral("window.tiled"), p);
        a.setDecorationProfileTree(tree);

        const auto stored = a.decorationProfileTree().directOverride(QStringLiteral("window.tiled"));
        QVERIFY(stored.presetIds.has_value());
        QCOMPARE(stored.presetIds->size(), 64);
        QVERIFY(!stored.presetIds->contains(QStringLiteral("glow")));
        QVERIFY(!stored.presetIds->contains(QStringLiteral("shadow")));
    }

    /// The sanitizer must be IDEMPOTENT and ORDER-STABLE.
    ///
    /// This tree serialises its overrides as a JSON ARRAY, so insertion order is
    /// observable and round-trips — unlike the overlay tree, whose object keys are
    /// sorted either way, which is why the overlay suite's existing second-pass test
    /// cannot see the mutation that matters here. Making `forEachInOrder` iterate the
    /// underlying hash, or `overriddenPaths()` sort, would leave this sanitizer
    /// order-unstable and fire `decorationProfileTreeChanged` on every repeat write,
    /// at slider-drag rate, while keeping every other test in this file green.
    void testDecorationProfileTree_sanitizerIsIdempotentAndOrderStable()
    {
        IsolatedConfigGuard guard;

        // Non-alphabetical on purpose, so an implementation that sorted instead of
        // preserving insertion order would fail rather than coincide.
        const QStringList inserted{QStringLiteral("window.tiled"), QStringLiteral("popup.zoneSelector"),
                                   QStringLiteral("osd")};
        PhosphorSurfaceShaders::DecorationProfileTree tree;
        for (const QString& path : inserted) {
            PhosphorSurfaceShaders::DecorationProfile p;
            p.chain = QStringList{QStringLiteral("glow")};
            tree.setOverride(path, p);
        }

        Settings settings;
        QSignalSpy spy(&settings, &Settings::decorationProfileTreeChanged);
        settings.setDecorationProfileTree(tree);
        QCOMPARE(spy.count(), 1);

        const auto firstPass = settings.decorationProfileTree();
        for (const QString& path : inserted) {
            QVERIFY2(firstPass.overriddenPaths().contains(path), qPrintable(path));
        }
        const QStringList firstOrder = firstPass.overriddenPaths();

        // A second pass over the sanitizer's OWN output must change nothing. That is
        // what makes the setter's equality gate reachable at all.
        settings.setDecorationProfileTree(firstPass);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(settings.decorationProfileTree().overriddenPaths(), firstOrder);
    }
};

QTEST_MAIN(TestSettingsDecorationTree)
#include "test_settings_decoration_tree.moc"
