// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_decorationpagecontroller.cpp
 * @brief Reader / mutator tests for DecorationPageController.
 *
 * The controller edits a PhosphorSurfaceShaders::DecorationProfileTree
 * carried by ISettings: per-surface chains with walk-up inheritance,
 * the empty path "" addressing the baseline. Pinned behaviour:
 *   - chainAt / resolvedProfile / rawProfile / hasOverride distinguish a
 *     direct override from an inherited value, and the baseline ("") from
 *     a leaf
 *   - setChain engages an override at a leaf and prunes per-pack
 *     parameters for packs dropped from the chain; clearOverride removes
 *     the override; the baseline "" cannot be cleared
 *   - setChainParam / setChainParams merge into the pack's existing
 *     parameters rather than replacing them
 *   - disabledPacksAt round-trips the per-surface disabled-pack set
 *   - parentChain walks self→ancestors; overrideDescendantCount /
 *     clearOverrideDescendants act on the strict subtree
 *   - profilesChanged re-fires from ISettings::decorationProfileTreeChanged
 *   - shaderEffectUsages lists the direct overrides using a given effect
 *
 * The decoration-SET surface (the controller's `setsBridge()` ShaderSetStore)
 * lives in its own TU, test_decoration_sets.cpp — same split, and for the same
 * reason, as the motion side's test_animations_motion_sets.cpp: to keep each
 * test file under the project's 800-line cap.
 *
 * The controller is constructed with a null SurfaceShaderRegistry — every
 * path exercised here is registry-independent (the registry only feeds the
 * available-packs listing, which is out of scope). Settings is a
 * TreeStubSettings: a StubSettings that actually stores the tree and emits
 * decorationProfileTreeChanged, so the controller's read-mutate-write loop
 * round-trips without the real PhosphorConfig::Store.
 */

#include <QSignalSpy>
#include <QTest>

#include <QStandardPaths>
#include <QStringList>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include "settings/decorationpagecontroller.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;

namespace {

/// StubSettings that genuinely stores the decoration tree (the base stub's
/// setter is a no-op) and emits decorationProfileTreeChanged on a real
/// change, so the controller's write-back path is observable. No Q_OBJECT:
/// like StubSettings it reuses the ISettings meta-object for the inherited
/// signal emits.
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

/// Seed a baseline chain plus a `window.tiled` leaf override so the reader
/// tests can tell inherited from directly-set.
void seedBaselinePlusLeaf(TreeStubSettings& settings)
{
    PhosphorSurfaceShaders::DecorationProfileTree tree;
    PhosphorSurfaceShaders::DecorationProfile baseline;
    baseline.chain = QStringList{QStringLiteral("border")};
    tree.setBaseline(baseline);

    PhosphorSurfaceShaders::DecorationProfile leaf;
    leaf.chain = QStringList{QStringLiteral("glow")};
    tree.setOverride(QStringLiteral("window.tiled"), leaf);

    settings.setDecorationProfileTree(tree);
}

} // namespace

class TestDecorationPageController : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Redirect GenericDataLocation to an isolated test tree. Nothing here
    /// writes to disk (the set CRUD moved to test_decoration_sets.cpp), but
    /// the controller resolves its sets directory from GenericDataLocation,
    /// so the redirect stays as a safety net against a future test that does.
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // ─── Readers ──────────────────────────────────────────────────────────

    /// chainAt resolves through the walk-up: the baseline chain for "",
    /// the leaf's own chain where an override exists, the inherited
    /// baseline chain for a sibling with no override.
    void chainAt_resolvesBaselineOverrideAndInherited()
    {
        TreeStubSettings settings;
        seedBaselinePlusLeaf(settings);
        DecorationPageController c(nullptr, &settings);

        QCOMPARE(c.chainAt(QString()), QStringList{QStringLiteral("border")});
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), QStringList{QStringLiteral("glow")});
        // window.snapped has no override — inherits the baseline chain.
        QCOMPARE(c.chainAt(QStringLiteral("window.snapped")), QStringList{QStringLiteral("border")});
    }

    /// hasOverride is true only where a direct override lives: never for
    /// the baseline "", never for an unsupported path, and false for a
    /// supported sibling that only inherits.
    void hasOverride_trueOnlyForDirectOverride()
    {
        TreeStubSettings settings;
        seedBaselinePlusLeaf(settings);
        DecorationPageController c(nullptr, &settings);

        QVERIFY(c.hasOverride(QStringLiteral("window.tiled")));
        QVERIFY(!c.hasOverride(QStringLiteral("window.snapped")));
        QVERIFY2(!c.hasOverride(QString()), "the baseline is not an override");
        QVERIFY2(!c.hasOverride(QStringLiteral("not.a.surface")), "unsupported path is never an override");
    }

    /// rawProfile is the sparse DIRECT profile: it carries a `chain` only
    /// where the surface directly sets one, and is empty for a surface
    /// that purely inherits. resolvedProfile always fills the effective
    /// chain after walk-up.
    void rawVsResolvedProfile_sparseDirectVsFilledEffective()
    {
        TreeStubSettings settings;
        seedBaselinePlusLeaf(settings);
        DecorationPageController c(nullptr, &settings);

        // Direct override → chain present in the sparse map.
        const QVariantMap rawLeaf = c.rawProfile(QStringLiteral("window.tiled"));
        QCOMPARE(rawLeaf.value(QStringLiteral("chain")).toStringList(), QStringList{QStringLiteral("glow")});

        // Pure inheritance → no direct fields, so the sparse map has no chain key.
        const QVariantMap rawInherited = c.rawProfile(QStringLiteral("window.snapped"));
        QVERIFY2(!rawInherited.contains(QStringLiteral("chain")), "an inheriting surface carries no direct chain");

        // Resolved view fills the effective (inherited) chain.
        const QVariantMap resolvedInherited = c.resolvedProfile(QStringLiteral("window.snapped"));
        QCOMPARE(resolvedInherited.value(QStringLiteral("chain")).toStringList(),
                 QStringList{QStringLiteral("border")});

        // Baseline "" resolves to itself.
        QCOMPARE(c.resolvedProfile(QString()).value(QStringLiteral("chain")).toStringList(),
                 QStringList{QStringLiteral("border")});
    }

    // ─── Chain mutation ───────────────────────────────────────────────────

    /// setChain engages a direct override at a leaf; clearOverride drops
    /// it back to inheritance. The baseline "" can never be cleared.
    void setChainThenClearOverride_engagesAndReleases()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        QVERIFY(!c.hasOverride(QStringLiteral("window.tiled")));

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border"), QStringLiteral("glow")});
        QVERIFY(c.hasOverride(QStringLiteral("window.tiled")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")),
                 (QStringList{QStringLiteral("border"), QStringLiteral("glow")}));

        QVERIFY(c.clearOverride(QStringLiteral("window.tiled")));
        QVERIFY(!c.hasOverride(QStringLiteral("window.tiled")));

        // Baseline is the root — clearing it is rejected.
        QVERIFY2(!c.clearOverride(QString()), "the baseline override cannot be cleared");
        // Clearing a surface with no override reports nothing removed.
        QVERIFY(!c.clearOverride(QStringLiteral("window.snapped")));
    }

    /// setChain on an unsupported path is a no-op guard — no override is
    /// created and the tree stays put.
    void setChain_rejectsUnsupportedPath()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        QSignalSpy spy(&c, &DecorationPageController::profilesChanged);
        c.setChain(QStringLiteral("not.a.surface"), QStringList{QStringLiteral("glow")});
        QCOMPARE(spy.count(), 0);
        QVERIFY(!c.hasOverride(QStringLiteral("not.a.surface")));
    }

    /// Dropping a pack from the chain prunes that pack's per-pack
    /// parameter override, so re-adding it later starts from defaults
    /// rather than resurrecting stale settings. A reorder (same packs)
    /// prunes nothing.
    void setChain_prunesParametersOfDroppedPacks()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        const QString path = QStringLiteral("window.tiled");
        c.setChain(path, QStringList{QStringLiteral("border"), QStringLiteral("glow")});
        c.setChainParam(path, QStringLiteral("glow"), QStringLiteral("intensity"), 5);
        c.setChainParam(path, QStringLiteral("border"), QStringLiteral("width"), 2);

        // Sanity: both packs' params engaged.
        {
            const QVariantMap params = c.rawProfile(path).value(QStringLiteral("parameters")).toMap();
            QVERIFY(params.contains(QStringLiteral("glow")));
            QVERIFY(params.contains(QStringLiteral("border")));
        }

        // Drop glow from the chain — its params must be pruned, border's kept.
        c.setChain(path, QStringList{QStringLiteral("border")});
        const QVariantMap params = c.rawProfile(path).value(QStringLiteral("parameters")).toMap();
        QVERIFY2(!params.contains(QStringLiteral("glow")), "dropped pack's parameters must be pruned");
        QVERIFY2(params.contains(QStringLiteral("border")), "retained pack's parameters must survive");
    }

    /// setChain also prunes the disabledPacks entry of a dropped pack, the
    /// parallel of the parameters prune above — a re-added pack starts enabled
    /// rather than resurrecting a stale disabled flag.
    void setChain_prunesDisabledPacksOfDroppedPacks()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        const QString path = QStringLiteral("window.tiled");
        c.setChain(path, QStringList{QStringLiteral("border"), QStringLiteral("glow")});
        c.setChainLayerEnabled(path, QStringLiteral("glow"), false);
        QVERIFY2(c.disabledPacksAt(path).contains(QStringLiteral("glow")), "glow should be disabled after the toggle");

        // Drop glow from the chain — its disabled flag must be pruned too.
        c.setChain(path, QStringList{QStringLiteral("border")});
        QVERIFY2(!c.disabledPacksAt(path).contains(QStringLiteral("glow")),
                 "dropped pack's disabledPacks entry must be pruned");
    }

    /// setChainLayerEnabled toggles a layer's disabled state, and its first
    /// direct edit at a leaf SEEDS from the resolved (inherited) disabled set
    /// so an inherited-off layer is preserved rather than silently re-enabled.
    void setChainLayerEnabled_togglesAndSeedsFromResolved()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        // Parent chain with two packs; disable glow at the parent.
        c.setChain(QStringLiteral("window"), QStringList{QStringLiteral("border"), QStringLiteral("glow")});
        c.setChainLayerEnabled(QStringLiteral("window"), QStringLiteral("glow"), false);
        QVERIFY(c.disabledPacksAt(QStringLiteral("window")).contains(QStringLiteral("glow")));

        // The child leaf has no own disabledPacks override yet; it inherits
        // glow-off from the parent.
        QVERIFY(c.disabledPacksAt(QStringLiteral("window.tiled")).contains(QStringLiteral("glow")));

        // First direct edit at the child: disable border. The seed-from-resolved
        // branch must PRESERVE the inherited glow-off rather than dropping it.
        c.setChainLayerEnabled(QStringLiteral("window.tiled"), QStringLiteral("border"), false);
        const QStringList disabled = c.disabledPacksAt(QStringLiteral("window.tiled"));
        QVERIFY2(disabled.contains(QStringLiteral("border")), "the just-disabled layer must be off");
        QVERIFY2(disabled.contains(QStringLiteral("glow")), "the inherited-off layer must be preserved on first edit");

        // Re-enabling border clears only it.
        c.setChainLayerEnabled(QStringLiteral("window.tiled"), QStringLiteral("border"), true);
        QVERIFY(!c.disabledPacksAt(QStringLiteral("window.tiled")).contains(QStringLiteral("border")));
    }

    /// disabledPacksAt resolves through inheritance like chainAt does — a leaf
    /// with no own disabledPacks override reports its ancestor's disabled set.
    void disabledPacksAt_resolvesThroughInheritance()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        c.setChain(QString(), QStringList{QStringLiteral("border"), QStringLiteral("glow")});
        c.setChainLayerEnabled(QString(), QStringLiteral("glow"), false);
        // A leaf with no own override inherits the baseline's disabled set.
        QVERIFY(c.disabledPacksAt(QStringLiteral("window.floating")).contains(QStringLiteral("glow")));
    }

    /// The window.floating leaf resolves its overlay like the other leaves — a
    /// direct override wins over the inherited baseline chain.
    void windowFloating_resolvesOverlayOverBaseline()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        c.setChain(QString(), QStringList{QStringLiteral("border")});
        c.setChain(QStringLiteral("window.floating"), QStringList{QStringLiteral("glow")});
        QCOMPARE(c.chainAt(QStringLiteral("window.floating")), (QStringList{QStringLiteral("glow")}));
        // A sibling with no own override still inherits the baseline.
        QCOMPARE(c.chainAt(QStringLiteral("window.snapped")), (QStringList{QStringLiteral("border")}));
    }

    /// setChain with the EMPTY path edits the baseline chain itself (the
    /// "All Windows"-style root card's edit path) — the empty path skips the
    /// supported-path guard by design and lands on the tree's baseline.
    void setChain_emptyPathEditsBaseline()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        QSignalSpy spy(&c, &DecorationPageController::profilesChanged);
        c.setChain(QString(), QStringList{QStringLiteral("glow")});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(c.chainAt(QString()), (QStringList{QStringLiteral("glow")}));
        // Every path with no own override now inherits the new baseline chain.
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));
    }

    /// setChain with an EMPTY chain engages an explicit "no decoration"
    /// override — the engaged-empty chain wins over the inherited baseline
    /// rather than falling through to it.
    void setChain_emptyChainEngagesNoDecorationOverride()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        const QString path = QStringLiteral("window.tiled");
        c.setChain(QString(), QStringList{QStringLiteral("border")});
        QCOMPARE(c.chainAt(path), (QStringList{QStringLiteral("border")})); // inherited

        c.setChain(path, QStringList{});
        QVERIFY2(c.hasOverride(path), "an engaged empty chain is still a direct override");
        QVERIFY2(c.chainAt(path).isEmpty(), "engaged empty chain must win over the inherited baseline");

        // Clearing the override restores inheritance.
        QVERIFY(c.clearOverride(path));
        QCOMPARE(c.chainAt(path), (QStringList{QStringLiteral("border")}));
    }

    /// setChainParams merges its keys into the pack's existing parameter
    /// map — a prior single-key setChainParam value is preserved.
    void setChainParams_mergesIntoExistingPackParams()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        const QString path = QStringLiteral("osd");
        c.setChain(path, QStringList{QStringLiteral("border")});
        c.setChainParam(path, QStringLiteral("border"), QStringLiteral("width"), 3);

        QVariantMap incoming;
        incoming.insert(QStringLiteral("radius"), 8);
        incoming.insert(QStringLiteral("color"), QStringLiteral("#ffffff"));
        c.setChainParams(path, QStringLiteral("border"), incoming);

        const QVariantMap border =
            c.rawProfile(path).value(QStringLiteral("parameters")).toMap().value(QStringLiteral("border")).toMap();
        QCOMPARE(border.value(QStringLiteral("width")).toInt(), 3);
        QCOMPARE(border.value(QStringLiteral("radius")).toInt(), 8);
        QCOMPARE(border.value(QStringLiteral("color")).toString(), QStringLiteral("#ffffff"));
    }

    // ─── Taxonomy + subtree ops ─────────────────────────────────────────────

    /// parentChain is self→ancestors, deepest first, excluding the empty
    /// baseline.
    void parentChain_walksSelfToAncestors()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        QCOMPARE(c.parentChain(QStringLiteral("window.tiled")),
                 (QStringList{QStringLiteral("window.tiled"), QStringLiteral("window")}));
        QCOMPARE(c.parentChain(QStringLiteral("popup.snapAssist")),
                 (QStringList{QStringLiteral("popup.snapAssist"), QStringLiteral("popup")}));
        QCOMPARE(c.parentChain(QStringLiteral("osd")), QStringList{QStringLiteral("osd")});
        QCOMPARE(c.parentChain(QString()), QStringList{});
    }

    /// overrideDescendantCount counts strictly-below overrides; the parent
    /// node itself is excluded. clearOverrideDescendants clears exactly
    /// those and reports the count.
    void overrideDescendants_countAndClearSubtree()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        // Override the parent AND two children.
        c.setChain(QStringLiteral("window"), QStringList{QStringLiteral("border")});
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        c.setChain(QStringLiteral("window.snapped"), QStringList{QStringLiteral("glow")});

        // Two descendants shadow "window"; the parent itself is not counted.
        QCOMPARE(c.overrideDescendantCount(QStringLiteral("window")), 2);

        const int cleared = c.clearOverrideDescendants(QStringLiteral("window"));
        QCOMPARE(cleared, 2);
        QVERIFY(!c.hasOverride(QStringLiteral("window.tiled")));
        QVERIFY(!c.hasOverride(QStringLiteral("window.snapped")));
        // The parent node's own override survives the subtree clear.
        QVERIFY(c.hasOverride(QStringLiteral("window")));
        QCOMPARE(c.overrideDescendantCount(QStringLiteral("window")), 0);
    }

    // ─── Reload propagation ─────────────────────────────────────────────────

    /// profilesChanged re-fires whenever the underlying ISettings emits
    /// decorationProfileTreeChanged — both from the controller's own
    /// write-back and from an external tree swap (a global reload).
    void profilesChanged_firesOnSettingsTreeChanged()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        QSignalSpy spy(&c, &DecorationPageController::profilesChanged);

        // Controller mutator writes the tree back → settings emits → we re-fire.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY2(spy.count() >= 1, "a controller write must re-fire profilesChanged");

        // External swap (simulating Settings::load() / Discard).
        const int before = spy.count();
        PhosphorSurfaceShaders::DecorationProfileTree other;
        PhosphorSurfaceShaders::DecorationProfile baseline;
        baseline.chain = QStringList{QStringLiteral("border")};
        other.setBaseline(baseline);
        settings.setDecorationProfileTree(other);
        QVERIFY2(spy.count() > before, "an external tree swap must re-fire profilesChanged");
    }

    // ─── Effect-usage query + param guard ───────────────────────────────────

    /// shaderEffectUsages lists exactly the DIRECT overrides whose chain
    /// contains the effect, sorted by label, and ignores the baseline / chains
    /// that don't use it. Registry-independent (a pure tree query).
    void shaderEffectUsages_listsDirectOverridesUsingTheEffect()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        c.setChain(QString(), QStringList{QStringLiteral("glow")}); // baseline uses glow but is not an override
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border"), QStringLiteral("glow")});
        c.setChain(QStringLiteral("osd"), QStringList{QStringLiteral("glow")});
        c.setChain(QStringLiteral("window.snapped"), QStringList{QStringLiteral("border")}); // no glow

        const QVariantList usages = c.shaderEffectUsages(QStringLiteral("glow"));
        QStringList paths;
        for (const QVariant& v : usages)
            paths << v.toMap().value(QStringLiteral("path")).toString();
        QCOMPARE(paths.size(), 2);
        QVERIFY2(paths.contains(QStringLiteral("window.tiled")), "the tiled override uses glow");
        QVERIFY2(paths.contains(QStringLiteral("osd")), "the osd override uses glow");
        QVERIFY2(!paths.contains(QStringLiteral("window.snapped")), "the snapped override does not use glow");
        QVERIFY2(!paths.contains(QString()), "the baseline is not reported as an override usage");

        // An effect nobody uses, and the empty id, both yield nothing.
        QVERIFY(c.shaderEffectUsages(QStringLiteral("nonexistent")).isEmpty());
        QVERIFY(c.shaderEffectUsages(QString()).isEmpty());
    }

    /// setChainParam / setChainParams reject an unsupported surface path (the
    /// same guard setChain has), leaving no override and firing no change.
    void setChainParam_rejectsUnsupportedPath()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        QSignalSpy spy(&c, &DecorationPageController::profilesChanged);
        c.setChainParam(QStringLiteral("not.a.surface"), QStringLiteral("border"), QStringLiteral("width"), 3);
        c.setChainParams(QStringLiteral("not.a.surface"), QStringLiteral("border"),
                         QVariantMap{{QStringLiteral("width"), 3}});
        QCOMPARE(spy.count(), 0);
        QVERIFY(!c.hasOverride(QStringLiteral("not.a.surface")));
    }
};

QTEST_MAIN(TestDecorationPageController)
#include "test_decorationpagecontroller.moc"
