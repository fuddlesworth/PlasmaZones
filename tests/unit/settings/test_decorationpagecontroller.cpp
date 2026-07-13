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

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include "config/configdefaults.h"
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

/// Author a pack directory `<root>/<subdir>/metadata.json` plus an effect.frag
/// stub so the registry's on-disk existence checks pass (mirrors the
/// SurfaceShaderRegistry test fixture).
bool writePack(const QString& root, const QString& subdir, const QJsonObject& metadata)
{
    const QString packDir = root + QLatin1Char('/') + subdir;
    const QFileInfo fi(packDir + QStringLiteral("/metadata.json"));
    if (!QDir().mkpath(fi.absolutePath()))
        return false;
    QFile meta(fi.absoluteFilePath());
    if (!meta.open(QIODevice::WriteOnly | QIODevice::Truncate) || meta.write(QJsonDocument(metadata).toJson()) < 0)
        return false;
    QFile frag(packDir + QStringLiteral("/effect.frag"));
    return frag.open(QIODevice::WriteOnly | QIODevice::Truncate) && frag.write(QByteArrayLiteral("// stub\n")) > 0;
}

/// Metadata for a border-providing pack declaring the shared contract param
/// ids. cornerRadius caps at 8 so the seed-clamp path is observable.
QJsonObject fancyBorderMetadata()
{
    const auto param = [](const char* id, const char* type, const QJsonValue& def, const QJsonValue& min = {},
                          const QJsonValue& max = {}) {
        QJsonObject p{{QLatin1String("id"), QLatin1String(id)},
                      {QLatin1String("type"), QLatin1String(type)},
                      {QLatin1String("default"), def}};
        if (!min.isUndefined())
            p.insert(QLatin1String("min"), min);
        if (!max.isUndefined())
            p.insert(QLatin1String("max"), max);
        return p;
    };
    return QJsonObject{{QLatin1String("id"), QLatin1String("fancy-border")},
                       {QLatin1String("name"), QLatin1String("Fancy Border")},
                       {QLatin1String("category"), QLatin1String("Borders")},
                       {QLatin1String("providesBorder"), true},
                       {QLatin1String("fragmentShader"), QLatin1String("effect.frag")},
                       {QLatin1String("parameters"),
                        QJsonArray{param("borderWidth", "int", 2, 0, 10), param("cornerRadius", "int", 8, 0, 8),
                                   param("activeColor", "color", QLatin1String("#ff3daee9")),
                                   param("inactiveColor", "color", QLatin1String("#ff888888"))}}};
}

/// Metadata for an opacity-tint-providing pack declaring the shared contract
/// param ids (opacity / tintStrength / tintColor).
QJsonObject fadeTintMetadata()
{
    const auto param = [](const char* id, const char* type, const QJsonValue& def) {
        return QJsonObject{{QLatin1String("id"), QLatin1String(id)},
                           {QLatin1String("type"), QLatin1String(type)},
                           {QLatin1String("default"), def},
                           {QLatin1String("min"), 0.0},
                           {QLatin1String("max"), 1.0}};
    };
    return QJsonObject{{QLatin1String("id"), QLatin1String("fade-tint")},
                       {QLatin1String("name"), QLatin1String("Fade Tint")},
                       {QLatin1String("category"), QLatin1String("Ambience")},
                       {QLatin1String("providesOpacityTint"), true},
                       {QLatin1String("fragmentShader"), QLatin1String("effect.frag")},
                       {QLatin1String("parameters"),
                        QJsonArray{param("opacity", "float", 1.0), param("tintStrength", "float", 0.0),
                                   QJsonObject{{QLatin1String("id"), QLatin1String("tintColor")},
                                               {QLatin1String("type"), QLatin1String("color")},
                                               {QLatin1String("default"), QLatin1String("#ff3daee9")}}}}};
}

/// A plain non-border pack: providesBorder absent, so setChain must not seed it.
QJsonObject glowishMetadata()
{
    return QJsonObject{{QLatin1String("id"), QLatin1String("glowish")},
                       {QLatin1String("name"), QLatin1String("Glowish")},
                       {QLatin1String("category"), QLatin1String("Effects")},
                       {QLatin1String("fragmentShader"), QLatin1String("effect.frag")},
                       {QLatin1String("parameters"),
                        QJsonArray{QJsonObject{{QLatin1String("id"), QLatin1String("intensity")},
                                               {QLatin1String("type"), QLatin1String("float")},
                                               {QLatin1String("default"), 1.0}}}}};
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

        // Author a look: baseline "border", window.tiled → "glow".
        c.setChain(QString(), QStringList{QStringLiteral("border")});
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("glow")});

        QSignalSpy setsSpy(&c, &DecorationPageController::decorationSetsChanged);
        QVERIFY(c.saveCurrentAsDecorationSet(QStringLiteral("My Look"), QStringLiteral("a test look")));
        QCOMPARE(setsSpy.count(), 1);

        // The set appears in the listing with the expected summary (baseline
        // counts as one covered surface, plus the single window.tiled override).
        const QVariantList sets = c.availableDecorationSets();
        QCOMPARE(sets.size(), 1);
        const QVariantMap set = sets.first().toMap();
        QCOMPARE(set.value(QStringLiteral("name")).toString(), QStringLiteral("My Look"));
        QCOMPARE(set.value(QStringLiteral("description")).toString(), QStringLiteral("a test look"));
        QCOMPARE(set.value(QStringLiteral("overrideCount")).toInt(), 2);

        // Mutate the live tree away from the saved look.
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border")});
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("border")}));

        // Apply restores the saved baseline + override.
        QVERIFY(c.applyDecorationSet(QStringLiteral("My Look")));
        QCOMPARE(c.chainAt(QString()), (QStringList{QStringLiteral("border")}));
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("glow")}));

        // Remove empties the listing and fires the change signal.
        QSignalSpy removeSpy(&c, &DecorationPageController::decorationSetsChanged);
        QVERIFY(c.removeDecorationSet(QStringLiteral("My Look")));
        QCOMPARE(removeSpy.count(), 1);
        QVERIFY(c.availableDecorationSets().isEmpty());
    }

    /// Saving with an empty tree (no baseline, no overrides) is refused: the
    /// resulting set would be a no-op that applyDecorationSet then rejects, so
    /// it must never reach disk.
    void saveDecorationSet_emptyTreeRejected()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        QSignalSpy setsSpy(&c, &DecorationPageController::decorationSetsChanged);
        QVERIFY2(!c.saveCurrentAsDecorationSet(QStringLiteral("Nothing"), QString()),
                 "saving an empty decoration tree must be refused");
        QCOMPARE(setsSpy.count(), 0);
        QVERIFY(c.availableDecorationSets().isEmpty());
    }

    /// saveCurrentAsDecorationSet rejects an empty name (would slugify to an
    /// empty filename).
    void saveDecorationSet_emptyNameRejected()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);
        c.setChain(QString(), QStringList{QStringLiteral("border")});
        QVERIFY(!c.saveCurrentAsDecorationSet(QString(), QString()));
    }

    /// applyDecorationSet validates every entry up-front and rejects the whole
    /// set on any unknown surface path, leaving the current tree untouched
    /// (atomic apply — no partial write).
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

        QVERIFY2(!c.applyDecorationSet(QStringLiteral("bad-set")), "a set with an unknown path must be rejected");
        // The valid entry must NOT have been applied — atomic all-or-nothing.
        QVERIFY2(!c.hasOverride(QStringLiteral("window.snapped")),
                 "applyDecorationSet wrote partial state from a malformed set");
        // The pre-existing override is untouched.
        QCOMPARE(c.chainAt(QStringLiteral("window.tiled")), (QStringList{QStringLiteral("border")}));
    }

    /// applyDecorationSet / removeDecorationSet on a name with no file report
    /// failure rather than crashing or emitting a spurious change.
    void decorationSets_unknownNameReturnsFalse()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        QSignalSpy setsSpy(&c, &DecorationPageController::decorationSetsChanged);
        QVERIFY(!c.applyDecorationSet(QStringLiteral("nonexistent")));
        QVERIFY(!c.removeDecorationSet(QStringLiteral("nonexistent")));
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

    /// A providesBorder pack newly added to a chain gets its shared contract
    /// params seeded from the plain Windows border setting: width copied,
    /// radius clamped to the pack's declared max, a concrete active colour
    /// copied, the "accent" sentinel skipped, a non-border pack untouched, and
    /// an already-present pack never re-seeded.
    void setChain_seedsBorderPackParamsFromPlainBorderSetting()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writePack(tmp.path(), QStringLiteral("fancy-border"), fancyBorderMetadata()));
        QVERIFY(writePack(tmp.path(), QStringLiteral("glowish"), glowishMetadata()));
        PhosphorSurfaceShaders::SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(registry.hasEffect(QStringLiteral("fancy-border")));

        TreeStubSettings settings;
        settings.setShowWindowBorder(true);
        settings.setWindowBorderWidth(4);
        settings.setWindowBorderRadius(12); // above the pack's max of 8
        settings.setWindowBorderColorActive(QStringLiteral("#80ff0000"));
        settings.setWindowBorderColorInactive(QStringLiteral("accent")); // sentinel, not a colour

        DecorationPageController c(&registry, &settings);
        const QString path = QStringLiteral("window.tiled");
        c.setChain(path, QStringList{QStringLiteral("fancy-border"), QStringLiteral("glowish")});

        const QVariantMap params = c.rawProfile(path).value(QStringLiteral("parameters")).toMap();
        const QVariantMap fancy = params.value(QStringLiteral("fancy-border")).toMap();
        QCOMPARE(fancy.value(QStringLiteral("borderWidth")).toInt(), 4);
        QCOMPARE(fancy.value(QStringLiteral("cornerRadius")).toInt(), 8);
        QCOMPARE(QColor(fancy.value(QStringLiteral("activeColor")).toString()), QColor(QStringLiteral("#80ff0000")));
        QVERIFY2(!fancy.contains(QStringLiteral("inactiveColor")), "accent sentinel must not seed a colour");
        QVERIFY2(!params.contains(QStringLiteral("glowish")), "non-border pack must not be seeded");

        // Re-writing the chain with the pack already present must not re-seed:
        // the user's (possibly edited) values are theirs now.
        settings.setWindowBorderWidth(9);
        c.setChain(path, QStringList{QStringLiteral("fancy-border")});
        const QVariantMap after = c.rawProfile(path)
                                      .value(QStringLiteral("parameters"))
                                      .toMap()
                                      .value(QStringLiteral("fancy-border"))
                                      .toMap();
        QCOMPARE(after.value(QStringLiteral("borderWidth")).toInt(), 4);
    }

    /// A providesOpacityTint pack newly added to a chain gets opacity /
    /// tintStrength / tintColor seeded from the plain opacity+tint setting,
    /// mirroring the border seeding. The plain border layer being off does not
    /// block it — each contract seeds under its own toggle.
    void setChain_seedsOpacityTintPackParamsFromPlainLayerSetting()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writePack(tmp.path(), QStringLiteral("fade-tint"), fadeTintMetadata()));
        PhosphorSurfaceShaders::SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(registry.hasEffect(QStringLiteral("fade-tint")));

        TreeStubSettings settings;
        settings.setShowWindowBorder(false);
        settings.setShowWindowOpacityTint(true);
        settings.setWindowOpacity(0.8);
        settings.setWindowTintStrength(0.25);
        settings.setWindowTintColor(QStringLiteral("#80ff0000"));

        DecorationPageController c(&registry, &settings);
        const QString path = QStringLiteral("window.tiled");
        c.setChain(path, QStringList{QStringLiteral("fade-tint")});

        const QVariantMap params =
            c.rawProfile(path).value(QStringLiteral("parameters")).toMap().value(QStringLiteral("fade-tint")).toMap();
        QCOMPARE(params.value(QStringLiteral("opacity")).toDouble(), 0.8);
        QCOMPARE(params.value(QStringLiteral("tintStrength")).toDouble(), 0.25);
        QCOMPARE(QColor(params.value(QStringLiteral("tintColor")).toString()), QColor(QStringLiteral("#80ff0000")));
    }

    /// With the plain border toggled off there is no look to carry over, so
    /// adding a border pack seeds nothing and the pack's own defaults apply.
    void setChain_noSeedWhenPlainBorderOff()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writePack(tmp.path(), QStringLiteral("fancy-border"), fancyBorderMetadata()));
        PhosphorSurfaceShaders::SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        TreeStubSettings settings;
        settings.setShowWindowBorder(false);
        settings.setWindowBorderWidth(4);

        DecorationPageController c(&registry, &settings);
        const QString path = QStringLiteral("window.tiled");
        c.setChain(path, QStringList{QStringLiteral("fancy-border")});

        const QVariantMap params = c.rawProfile(path).value(QStringLiteral("parameters")).toMap();
        QVERIFY2(!params.contains(QStringLiteral("fancy-border")), "no seeding while the plain border is off");
    }

    /// First direct edit at a path with NO override: prevChain falls back to
    /// the resolved effective chain, so a pack the user was already
    /// previewing via inheritance is not "new" and must not be seeded — the
    /// inherited preview showed the pack's own values, and seeding would
    /// change its look on a chain edit that kept it.
    void setChain_inheritedPreviewPackNotReseeded()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writePack(tmp.path(), QStringLiteral("fancy-border"), fancyBorderMetadata()));
        QVERIFY(writePack(tmp.path(), QStringLiteral("glowish"), glowishMetadata()));
        PhosphorSurfaceShaders::SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        TreeStubSettings settings;
        settings.setShowWindowBorder(true);
        settings.setWindowBorderWidth(4);
        // Baseline carries the pack; the leaf inherits it (no direct override).
        PhosphorSurfaceShaders::DecorationProfileTree tree;
        PhosphorSurfaceShaders::DecorationProfile baseline;
        baseline.chain = QStringList{QStringLiteral("fancy-border")};
        tree.setBaseline(baseline);
        settings.setDecorationProfileTree(tree);

        DecorationPageController c(&registry, &settings);
        const QString path = QStringLiteral("window.tiled");
        c.setChain(path, QStringList{QStringLiteral("fancy-border"), QStringLiteral("glowish")});

        const QVariantMap params = c.rawProfile(path).value(QStringLiteral("parameters")).toMap();
        QVERIFY2(!params.contains(QStringLiteral("fancy-border")),
                 "a pack inherited into the previous effective chain must not be seeded");
    }

    /// Seeding at an inheriting path must not drop OTHER packs' inherited
    /// params: engaging the direct parameters override (which
    /// DecorationProfile::overlay replaces wholesale) has to start from the
    /// resolved effective map, not an empty one, or the baseline's per-pack
    /// values vanish from the leaf the moment a providesBorder pack is added.
    void setChain_seedPreservesInheritedParams()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writePack(tmp.path(), QStringLiteral("fancy-border"), fancyBorderMetadata()));
        QVERIFY(writePack(tmp.path(), QStringLiteral("glowish"), glowishMetadata()));
        PhosphorSurfaceShaders::SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        TreeStubSettings settings;
        settings.setShowWindowBorder(true);
        settings.setWindowBorderWidth(4);
        // Baseline carries glowish WITH a tuned param; the leaf inherits both.
        PhosphorSurfaceShaders::DecorationProfileTree tree;
        PhosphorSurfaceShaders::DecorationProfile baseline;
        baseline.chain = QStringList{QStringLiteral("glowish")};
        baseline.parameters = QVariantMap{{QStringLiteral("glowish"), QVariantMap{{QStringLiteral("intensity"), 5.0}}}};
        tree.setBaseline(baseline);
        settings.setDecorationProfileTree(tree);

        DecorationPageController c(&registry, &settings);
        const QString path = QStringLiteral("window.tiled");
        c.setChain(path, QStringList{QStringLiteral("glowish"), QStringLiteral("fancy-border")});

        const QVariantMap params = c.rawProfile(path).value(QStringLiteral("parameters")).toMap();
        QCOMPARE(params.value(QStringLiteral("fancy-border")).toMap().value(QStringLiteral("borderWidth")).toInt(), 4);
        QCOMPARE(params.value(QStringLiteral("glowish")).toMap().value(QStringLiteral("intensity")).toDouble(), 5.0);
    }

    /// setChainParam at an INHERITING path engages the parameters override
    /// from the resolved effective map, so a first per-param edit for one
    /// pack must not drop a sibling pack's inherited params (the same
    /// wholesale-overlay hazard as the setChain seed). Params for a pack an
    /// ancestor already removed from the chain must NOT be materialized.
    void setChainParam_engagesFromResolvedInheritedParams()
    {
        TreeStubSettings settings;
        PhosphorSurfaceShaders::DecorationProfileTree tree;
        PhosphorSurfaceShaders::DecorationProfile baseline;
        baseline.chain = QStringList{QStringLiteral("glow"), QStringLiteral("border")};
        baseline.parameters = QVariantMap{{QStringLiteral("glow"), QVariantMap{{QStringLiteral("glowSize"), 24}}},
                                          {QStringLiteral("border"), QVariantMap{{QStringLiteral("borderWidth"), 3}}},
                                          {QStringLiteral("stale-pack"), QVariantMap{{QStringLiteral("x"), 1}}}};
        tree.setBaseline(baseline);
        settings.setDecorationProfileTree(tree);

        DecorationPageController c(nullptr, &settings);
        const QString path = QStringLiteral("window.tiled");
        c.setChainParam(path, QStringLiteral("glow"), QStringLiteral("glowSize"), 32);

        const QVariantMap params = c.rawProfile(path).value(QStringLiteral("parameters")).toMap();
        QCOMPARE(params.value(QStringLiteral("glow")).toMap().value(QStringLiteral("glowSize")).toInt(), 32);
        QCOMPARE(params.value(QStringLiteral("border")).toMap().value(QStringLiteral("borderWidth")).toInt(), 3);
        QVERIFY2(!params.contains(QStringLiteral("stale-pack")),
                 "params for a pack outside the effective chain must not be materialized");
    }

    /// A providesBorder pack that declares only a subset of the shared
    /// contract ids (the border-rgb / border-double shapes) is seeded for
    /// exactly the ids it declares — the missing ids are skipped, not
    /// inserted.
    void setChain_seedSkipsUndeclaredParamIds()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QJsonObject slim{{QLatin1String("id"), QLatin1String("slim-border")},
                               {QLatin1String("name"), QLatin1String("Slim Border")},
                               {QLatin1String("category"), QLatin1String("Borders")},
                               {QLatin1String("providesBorder"), true},
                               {QLatin1String("fragmentShader"), QLatin1String("effect.frag")},
                               {QLatin1String("parameters"),
                                QJsonArray{QJsonObject{{QLatin1String("id"), QLatin1String("borderWidth")},
                                                       {QLatin1String("type"), QLatin1String("int")},
                                                       {QLatin1String("default"), 2},
                                                       {QLatin1String("min"), 0},
                                                       {QLatin1String("max"), 10}}}}};
        QVERIFY(writePack(tmp.path(), QStringLiteral("slim-border"), slim));
        PhosphorSurfaceShaders::SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        TreeStubSettings settings;
        settings.setShowWindowBorder(true);
        settings.setWindowBorderWidth(4);
        settings.setWindowBorderRadius(6);
        settings.setWindowBorderColorActive(QStringLiteral("#80ff0000"));
        settings.setWindowBorderColorInactive(QStringLiteral("#80888888"));

        DecorationPageController c(&registry, &settings);
        const QString path = QStringLiteral("window.tiled");
        c.setChain(path, QStringList{QStringLiteral("slim-border")});

        const QVariantMap params =
            c.rawProfile(path).value(QStringLiteral("parameters")).toMap().value(QStringLiteral("slim-border")).toMap();
        QCOMPARE(params.value(QStringLiteral("borderWidth")).toInt(), 4);
        QVERIFY2(!params.contains(QStringLiteral("cornerRadius")), "undeclared id must not be inserted");
        QVERIFY2(!params.contains(QStringLiteral("activeColor")), "undeclared id must not be inserted");
        QVERIFY2(!params.contains(QStringLiteral("inactiveColor")), "undeclared id must not be inserted");
    }
};

QTEST_MAIN(TestDecorationPageController)
#include "test_decorationpagecontroller.moc"
