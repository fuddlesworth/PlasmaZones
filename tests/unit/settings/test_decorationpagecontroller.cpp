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
 *   - parentChain walks self→ancestors; overrideDescendantCount /
 *     clearOverrideDescendants act on the strict subtree
 *   - profilesChanged re-fires from ISettings::decorationProfileTreeChanged
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

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include "config/configdefaults.h"
#include "settings/decorationpagecontroller.h"
#include "settings/shadersetstore.h"
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

/// Absolute path to the decoration-sets directory the controller writes to,
/// recomputed the same way DecorationPageController::decorationSetsDirectoryPath
/// does. Valid only under QStandardPaths test mode (see initTestCase), which
/// redirects GenericDataLocation to an isolated per-user test tree.
QString decorationSetsDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userDecorationSetsSubdir());
}

class TestDecorationPageController : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Redirect GenericDataLocation to an isolated test tree so the
    /// decoration-set CRUD tests never touch the real ~/.local/share.
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    /// Each test starts from an empty sets directory.
    void init()
    {
        QDir(decorationSetsDir()).removeRecursively();
    }

    void cleanupTestCase()
    {
        QDir(decorationSetsDir()).removeRecursively();
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

    // ─── Decoration sets (save / list / apply / remove) ─────────────────────

    /// A decoration set snapshots the baseline + per-surface overrides to a
    /// JSON file, and applying it merges those overrides back into the current
    /// tree. Full round-trip: save a look, mutate the tree, apply, and confirm
    /// the saved chains are restored; then remove and confirm the listing empties.
    void decorationSets_saveListApplyRemoveRoundTrips()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();
        QVERIFY(sets);

        // Author a look: baseline "border", window.tiled → "glow".
        c.setChain(QString(), QStringList{QStringLiteral("border")});
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("My Look"), QStringLiteral("a test look")));
        QCOMPARE(setsSpy.count(), 1);

        // The set appears in the listing with the expected summary (baseline
        // counts as one covered surface, plus the single window.tiled
        // override), and it is `active` because it still matches the live tree.
        const QVariantList saved = sets->availableSets();
        QCOMPARE(saved.size(), 1);
        const QVariantMap set = saved.first().toMap();
        QCOMPARE(set.value(QStringLiteral("name")).toString(), QStringLiteral("My Look"));
        QCOMPARE(set.value(QStringLiteral("description")).toString(), QStringLiteral("a test look"));
        QCOMPARE(set.value(QStringLiteral("coverageCount")).toInt(), 2);
        QCOMPARE(set.value(QStringLiteral("hasBaseline")).toBool(), true);
        QCOMPARE(set.value(QStringLiteral("coverage")).toStringList(), (QStringList{QStringLiteral("window")}));
        QVERIFY2(set.value(QStringLiteral("active")).toBool(), "a just-saved set must read as active");

        // Mutate the live tree away from the saved look.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("border")}));
        QVERIFY2(!sets->availableSets().first().toMap().value(QStringLiteral("active")).toBool(),
                 "editing the live tree away from the set must clear its active flag");

        // Apply restores the saved baseline + override.
        QVERIFY(sets->applySet(QStringLiteral("My Look")));
        QCOMPARE(c.chainAt(QString()), (QStringList{QStringLiteral("border")}));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));
        QVERIFY2(sets->availableSets().first().toMap().value(QStringLiteral("active")).toBool(),
                 "the set must read as active again right after applying it");

        // Remove empties the listing and fires the change signal.
        QSignalSpy removeSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(sets->removeSet(QStringLiteral("My Look")));
        QCOMPARE(removeSpy.count(), 1);
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// Apply MERGES, so an unrelated live override must not clear the badge:
    /// the set's own entries are all still live. An equality check (rather
    /// than containment) would wrongly read this as inactive.
    void decorationSets_activeSurvivesUnrelatedOverride()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Tiled Only"), QString()));

        // A surface the set does not cover — apply would have left it alone.
        c.setChain(QStringLiteral("osd"), QStringList{QStringLiteral("border")});

        QVERIFY2(sets->availableSets().first().toMap().value(QStringLiteral("active")).toBool(),
                 "an override outside the set's coverage must not clear its active flag");
    }

    /// updateSet keeps the payload while renaming and editing the
    /// description, and frees the old name. Renaming onto an existing set is
    /// refused rather than destroying it.
    void decorationSets_updateRoundTripsAndRefusesCollision()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Old Name"), QStringLiteral("keep me")));
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Other"), QString()));

        QVERIFY(sets->updateSet(QStringLiteral("Old Name"), QStringLiteral("New Name"), QStringLiteral("new words")));
        const QVariantList after = sets->availableSets();
        QCOMPARE(after.size(), 2);
        QStringList names;
        for (const QVariant& row : after)
            names << row.toMap().value(QStringLiteral("name")).toString();
        names.sort();
        QCOMPARE(names, (QStringList{QStringLiteral("New Name"), QStringLiteral("Other")}));
        for (const QVariant& row : after) {
            if (row.toMap().value(QStringLiteral("name")).toString() == QLatin1String("New Name"))
                QCOMPARE(row.toMap().value(QStringLiteral("description")).toString(), QStringLiteral("new words"));
        }

        // The payload survived the rename: applying restores the saved chain.
        QVERIFY(sets->applySet(QStringLiteral("New Name")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));

        // A same-name call is a description-only edit; an empty description
        // clears the field (stored as an absent key, like save).
        QVERIFY(sets->updateSet(QStringLiteral("New Name"), QStringLiteral("New Name"), QString()));
        for (const QVariant& row : sets->availableSets()) {
            if (row.toMap().value(QStringLiteral("name")).toString() == QLatin1String("New Name"))
                QVERIFY(row.toMap().value(QStringLiteral("description")).toString().isEmpty());
        }

        // Renaming onto a name that is taken must not clobber the other set.
        QVERIFY2(!sets->updateSet(QStringLiteral("New Name"), QStringLiteral("Other"), QString()),
                 "rename onto an existing set must be refused");
        QCOMPARE(sets->availableSets().size(), 2);
    }

    /// Export writes a file that import reads back. Importing while the
    /// original is still present must not overwrite it — the copy lands under
    /// a free name.
    void decorationSets_exportImportRoundTrips()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});
        QVERIFY(sets->saveCurrentAsSet(QStringLiteral("Portable"), QString()));

        QTemporaryDir exportDir;
        QVERIFY(exportDir.isValid());
        const QString exported = exportDir.filePath(QStringLiteral("portable.json"));
        QVERIFY(sets->exportSet(QStringLiteral("Portable"), exported));
        QVERIFY(QFile::exists(exported));

        // Re-importing alongside the original yields a second, distinct set.
        QVERIFY(sets->importSet(exported));
        QCOMPARE(sets->availableSets().size(), 2);

        // Deleting both and importing restores the set from the file alone.
        for (const QVariant& row : sets->availableSets())
            QVERIFY(sets->removeSet(row.toMap().value(QStringLiteral("name")).toString()));
        QVERIFY(sets->availableSets().isEmpty());

        QVERIFY(sets->importSet(exported));
        const QVariantList imported = sets->availableSets();
        QCOMPARE(imported.size(), 1);
        QCOMPARE(imported.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Portable"));

        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QVERIFY(sets->applySet(QStringLiteral("Portable")));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));
    }

    /// Import validates against the surface taxonomy, so a file carrying an
    /// unknown path is refused at the boundary rather than landing on disk and
    /// failing later at apply time.
    void decorationSets_importRejectsForeignPayload()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString foreign = dir.filePath(QStringLiteral("foreign.json"));

        QJsonObject entry;
        entry.insert(QStringLiteral("path"), QStringLiteral("window.appearance.open")); // a motion path
        entry.insert(QStringLiteral("profile"), QJsonObject{});
        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("Foreign"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{entry});

        QFile f(foreign);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(root).toJson());
        f.close();

        QVERIFY2(!sets->importSet(foreign), "a set whose paths are not decoration surfaces must be refused");
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// Saving with an empty tree (no baseline, no overrides) is refused: the
    /// resulting set would be a no-op that applySet then rejects, so it must
    /// never reach disk.
    void saveDecorationSet_emptyTreeRejected()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY2(!sets->saveCurrentAsSet(QStringLiteral("Nothing"), QString()),
                 "saving an empty decoration tree must be refused");
        QCOMPARE(setsSpy.count(), 0);
        QVERIFY(sets->availableSets().isEmpty());
    }

    /// saveCurrentAsSet rejects an empty name (would slugify to an empty
    /// filename).
    void saveDecorationSet_emptyNameRejected()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setChain(QString(), QStringList{QStringLiteral("border")});
        QVERIFY(!c.setsBridge()->saveCurrentAsSet(QString(), QString()));
    }

    /// applySet validates every entry up-front and rejects the whole set on any
    /// unknown surface path, leaving the current tree untouched (atomic apply —
    /// no partial write).
    void applyDecorationSet_unknownPathRejectsWholeSetAtomically()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});

        // Hand-craft a set file mixing a valid and an unknown-path entry.
        QVERIFY(QDir().mkpath(decorationSetsDir()));
        QJsonObject validProfile;
        validProfile.insert(QStringLiteral("chain"), QJsonArray{QStringLiteral("glow")});
        QJsonObject validEntry;
        validEntry.insert(QStringLiteral("path"), QStringLiteral("window.snapped"));
        validEntry.insert(QStringLiteral("profile"), validProfile);
        QJsonObject badEntry;
        badEntry.insert(QStringLiteral("path"), QStringLiteral("../etc/passwd"));
        badEntry.insert(QStringLiteral("profile"), QJsonObject{});

        QJsonObject root;
        root.insert(QStringLiteral("name"), QStringLiteral("bad-set"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("overrides"), QJsonArray{validEntry, badEntry});

        QFile f(decorationSetsDir() + QStringLiteral("/bad-set.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray bytes = QJsonDocument(root).toJson();
        QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
        f.close();

        QVERIFY2(!c.setsBridge()->applySet(QStringLiteral("bad-set")), "a set with an unknown path must be rejected");
        // The valid entry must NOT have been applied — atomic all-or-nothing.
        QVERIFY2(!c.hasOverride(QStringLiteral("window.snapped")), "applySet wrote partial state from a malformed set");
        // The pre-existing override is untouched.
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("border")}));
    }

    /// applySet / removeSet on a name with no file report failure rather than
    /// crashing or emitting a spurious change.
    void decorationSets_unknownNameReturnsFalse()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        ShaderSetStore* sets = c.setsBridge();

        QSignalSpy setsSpy(sets, &ShaderSetStore::setsChanged);
        QVERIFY(!sets->applySet(QStringLiteral("nonexistent")));
        QVERIFY(!sets->removeSet(QStringLiteral("nonexistent")));
        QCOMPARE(setsSpy.count(), 0);
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
