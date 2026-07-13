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

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include "settings/decorationpagecontroller.h"
#include "../helpers/TreeStubSettings.h"

using namespace PlasmaZones;

namespace {

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

    /// shaderEffectUsages lists every chain the effect appears in, sorted by
    /// label: the direct overrides AND the baseline, which the resolve walk falls
    /// back to (D-Bus can set one, so a pack used only there is still in use).
    /// Chains that do not use it are ignored. Registry-independent (a pure tree
    /// query).
    void shaderEffectUsages_listsDirectOverridesUsingTheEffect()
    {
        TreeStubSettings settings;
        DecorationPageController c(nullptr, &settings);

        c.setChain(QString(), QStringList{QStringLiteral("glow")}); // the baseline uses glow too
        c.setChain(QStringLiteral("window.tiled"), QStringList{QStringLiteral("border"), QStringLiteral("glow")});
        c.setChain(QStringLiteral("osd"), QStringList{QStringLiteral("glow")});
        c.setChain(QStringLiteral("window.snapped"), QStringList{QStringLiteral("border")}); // no glow

        const QVariantList usages = c.shaderEffectUsages(QStringLiteral("glow"));
        QStringList paths;
        for (const QVariant& v : usages)
            paths << v.toMap().value(QStringLiteral("path")).toString();
        QCOMPARE(paths.size(), 3);
        QVERIFY2(paths.contains(QStringLiteral("window.tiled")), "the tiled override uses glow");
        QVERIFY2(paths.contains(QStringLiteral("osd")), "the osd override uses glow");
        QVERIFY2(!paths.contains(QStringLiteral("window.snapped")), "the snapped override does not use glow");
        QVERIFY2(paths.contains(QString()), "the baseline uses glow, and it is a real chain the tree resolves through");

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

    /// The opacity-tint analogue: with ShowWindowOpacityTint off there is no plain look to
    /// carry over, so adding an opacity-tint pack seeds nothing. Guards the seed's gate on
    /// its OWN toggle (a seed that ignored showWindowOpacityTint would go uncaught otherwise,
    /// since the positive test above only proves the toggle-on path).
    void setChain_noSeedWhenPlainOpacityTintOff()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writePack(tmp.path(), QStringLiteral("fade-tint"), fadeTintMetadata()));
        PhosphorSurfaceShaders::SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);

        TreeStubSettings settings;
        settings.setShowWindowOpacityTint(false);
        settings.setWindowOpacity(0.8);
        settings.setWindowTintStrength(0.25);

        DecorationPageController c(&registry, &settings);
        const QString path = QStringLiteral("window.tiled");
        c.setChain(path, QStringList{QStringLiteral("fade-tint")});

        const QVariantMap params = c.rawProfile(path).value(QStringLiteral("parameters")).toMap();
        QVERIFY2(!params.contains(QStringLiteral("fade-tint")), "no seeding while the plain opacity-tint is off");
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

    /// availableShaderEffects() offers EVERY registered pack, including "border" and
    /// "opacity-tint". Those two also back the plain easy-mode layers, but they are
    /// selectable packs like any other: the effect injects the plain layer only for a
    /// window whose chain has no user packs, so picking one cannot double-apply. This pins
    /// the removal of the old picker filter that hid exactly those two ids.
    void availableShaderEffects_offersBorderAndOpacityTintPacks()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // Author the two reserved-named packs plus an ordinary one, so a filter keyed on
        // the id (the bug this guards against) would drop the first two and leave the third.
        QJsonObject borderMeta = fancyBorderMetadata();
        borderMeta.insert(QLatin1String("id"), QLatin1String("border"));
        QJsonObject opacityTintMeta = fadeTintMetadata();
        opacityTintMeta.insert(QLatin1String("id"), QLatin1String("opacity-tint"));
        QVERIFY(writePack(tmp.path(), QStringLiteral("border"), borderMeta));
        QVERIFY(writePack(tmp.path(), QStringLiteral("opacity-tint"), opacityTintMeta));
        QVERIFY(writePack(tmp.path(), QStringLiteral("glowish"), glowishMetadata()));

        PhosphorSurfaceShaders::SurfaceShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(registry.hasEffect(QStringLiteral("border")));
        QVERIFY(registry.hasEffect(QStringLiteral("opacity-tint")));

        TreeStubSettings settings;
        DecorationPageController c(&registry, &settings);

        QStringList offeredIds;
        const QVariantList offered = c.availableShaderEffects();
        for (const QVariant& entry : offered) {
            offeredIds.append(entry.toMap().value(QStringLiteral("id")).toString());
        }
        QVERIFY2(offeredIds.contains(QStringLiteral("border")), "the border pack must be selectable");
        QVERIFY2(offeredIds.contains(QStringLiteral("opacity-tint")), "the opacity-tint pack must be selectable");
        QVERIFY2(offeredIds.contains(QStringLiteral("glowish")), "an ordinary pack must still be offered");
    }
};

QTEST_MAIN(TestDecorationPageController)
#include "test_decorationpagecontroller.moc"
