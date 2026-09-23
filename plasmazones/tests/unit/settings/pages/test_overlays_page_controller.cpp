// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_overlays_page_controller.cpp
 * @brief Reader / mutator tests for OverlaysPageController.
 *
 * The controller edits the OverlayShaderTree carried by ISettings: one
 * global baseline (path "") plus flat per-layout-UUID overrides. Pinned
 * behaviour:
 *   - hasOverride / rawShaderProfile / resolvedShaderProfile distinguish a
 *     direct override from the inherited baseline, and "" from a layout
 *   - setShaderOverride engages the baseline arm for "" and the override
 *     arm otherwise; clearOverride removes an override, returns false when
 *     none exists, and rejects ""
 *   - shaderEffectUsages lists the baseline row first, then override rows
 *     sorted case-insensitively by label, and names a layout the registry
 *     cannot find with the shared absent-layout wording rather than leaving
 *     the label empty (an empty label made the browser fall back to the raw
 *     36-character UUID)
 *   - assignableLayouts sorts the live rows and appends every override the
 *     registry cannot name, flagged `missing` — the only UI path to a dead
 *     override
 *   - shaderProfileChanged re-fires from ISettings::overlayShaderTreeChanged,
 *     naming the node when this controller made the write and reporting a
 *     whole-tree change when anything else did
 *   - nodeState agrees with the three separate reads it replaces
 *
 * Most slots construct with null shader and layout registries: those paths are
 * registry-independent (the registries only feed the pack listing and layout
 * enumeration), and the constructor documents nullptr seams for exactly this
 * use. The two slots that DO need naming and sorting build a real
 * LayoutRegistry, because a null registry makes every label identical and any
 * sort or name lookup vacuous. Settings is StubSettings, whose overlay tree
 * accessors store and emit for real.
 */

#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "core/types/overlayshadertree.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "settings/pages/overlayspagecontroller.h"

using namespace PlasmaZones;

namespace {
const QString kLayoutA = QStringLiteral("{aaaa0000-0000-0000-0000-000000000000}");
const QString kLayoutB = QStringLiteral("{bbbb0000-0000-0000-0000-000000000000}");
} // namespace

class TestOverlaysPageController : public QObject
{
    Q_OBJECT

private:
    /// A registry holding @p names, in the order given, with the ids the
    /// caller can read back off the returned layouts.
    static PhosphorZones::LayoutRegistry* registryWith(const QStringList& names, QObject* parent)
    {
        auto* registry = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), parent);
        for (const QString& name : names)
            registry->addLayout(new PhosphorZones::Layout(name));
        return registry;
    }

private Q_SLOTS:
    void testNullSeams_returnEmptyAndNoop()
    {
        OverlaysPageController c(nullptr, nullptr, nullptr, nullptr);
        QVERIFY(c.assignableLayouts().isEmpty());
        QVERIFY(!c.hasOverride(kLayoutA));
        QCOMPARE(c.rawShaderProfile(QString()).value(QStringLiteral("shaderId")).toString(), QString());
        QVERIFY(c.availableShaderEffects().isEmpty());
        QVERIFY(c.shaderEffectUsages(QStringLiteral("x")).isEmpty());
        c.setShaderOverride(kLayoutA, QStringLiteral("pack"), {});
        QVERIFY(!c.clearOverride(kLayoutA));
    }

    void testSetShaderOverride_baselineAndOverrideArms()
    {
        StubSettings settings;
        OverlaysPageController c(nullptr, nullptr, &settings, nullptr);

        c.setShaderOverride(QString(), QStringLiteral("baseline-pack"), {{QStringLiteral("speed"), 1.5}});
        QVERIFY(!c.hasOverride(QString())); // "" is the baseline, never an override
        QCOMPARE(c.rawShaderProfile(QString()).value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("baseline-pack"));

        // A layout with no override resolves to the baseline but raw-reads empty.
        QVERIFY(!c.hasOverride(kLayoutA));
        QCOMPARE(c.resolvedShaderProfile(kLayoutA).value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("baseline-pack"));
        QCOMPARE(c.rawShaderProfile(kLayoutA).value(QStringLiteral("shaderId")).toString(), QString());

        c.setShaderOverride(kLayoutA, QStringLiteral("override-pack"), {{QStringLiteral("k"), 1}});
        QVERIFY(c.hasOverride(kLayoutA));
        QCOMPARE(c.resolvedShaderProfile(kLayoutA).value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("override-pack"));
        QCOMPARE(c.resolvedShaderProfile(kLayoutA)
                     .value(QStringLiteral("parameters"))
                     .toMap()
                     .value(QStringLiteral("k"))
                     .toInt(),
                 1);
        // The other layout still inherits.
        QCOMPARE(c.resolvedShaderProfile(kLayoutB).value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("baseline-pack"));
    }

    void testClearOverride_contract()
    {
        StubSettings settings;
        OverlaysPageController c(nullptr, nullptr, &settings, nullptr);
        c.setShaderOverride(kLayoutA, QStringLiteral("pack"), {});

        QVERIFY(!c.clearOverride(QString())); // baseline is rejected
        QVERIFY(c.clearOverride(kLayoutA));
        QVERIFY(!c.hasOverride(kLayoutA));
        QVERIFY(!c.clearOverride(kLayoutA)); // second clear finds nothing
    }

    /// assignableLayouts with a real registry: the live rows come first sorted
    /// case-insensitively by name, and every override the registry cannot name
    /// is appended after them flagged `missing`. That append is the ONLY UI
    /// path to a dead override, so without it an assignment for a deleted
    /// layout is invisible and unclearable. Nothing else covers the registry
    /// half of this controller.
    void testAssignableLayouts_liveRowsSortedThenOrphanOverridesAppended()
    {
        PlasmaZones::TestHelpers::IsolatedConfigGuard guard;
        QObject owner;
        auto* registry = registryWith({QStringLiteral("zebra"), QStringLiteral("Alpha")}, &owner);
        const QString zebraId = registry->layoutByName(QStringLiteral("zebra"))->id().toString();
        const QString alphaId = registry->layoutByName(QStringLiteral("Alpha"))->id().toString();

        StubSettings settings;
        OverlaysPageController c(nullptr, registry, &settings, nullptr);
        c.setShaderOverride(zebraId, QStringLiteral("pack"), {});
        c.setShaderOverride(kLayoutA, QStringLiteral("pack"), {}); // no such layout here

        const QVariantList rows = c.assignableLayouts();
        QCOMPARE(rows.size(), 3);
        // Case-insensitive: "Alpha" before "zebra" despite the capital.
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Alpha"));
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("id")).toString(), alphaId);
        QVERIFY(!rows.at(0).toMap().value(QStringLiteral("missing")).toBool());
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("zebra"));
        // The orphan is appended AFTER the live rows, named by nothing, flagged.
        const QVariantMap orphan = rows.at(2).toMap();
        QCOMPARE(orphan.value(QStringLiteral("id")).toString(), kLayoutA);
        QVERIFY(orphan.value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY2(orphan.value(QStringLiteral("missing")).toBool(), "the dead override was not flagged missing");
        // A layout the registry HAS is never duplicated into the orphan tail.
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("id")).toString(), zebraId);
        QVERIFY(!rows.at(1).toMap().value(QStringLiteral("missing")).toBool());
    }

    /// The label sort in shaderEffectUsages, which the null-registry slot below
    /// cannot exercise: with no registry every label is the same absent-layout
    /// string, so deleting the sort stays green there.
    ///
    /// The rows arrive in overriddenLayouts() order, which is sorted by UUID.
    /// Naming is therefore assigned AFTER the ids are known, so that the id
    /// order is the exact reverse of the label order — otherwise a run whose
    /// random ids happened to agree with the labels would pass with no sort at
    /// all, which is what a first draft of this slot did.
    void testShaderEffectUsages_layoutRowsSortCaseInsensitivelyByLabel()
    {
        PlasmaZones::TestHelpers::IsolatedConfigGuard guard;
        QObject owner;
        auto* registry = registryWith({QStringLiteral("one"), QStringLiteral("two")}, &owner);
        QStringList ids{registry->layout(0)->id().toString(), registry->layout(1)->id().toString()};
        ids.sort();
        registry->layoutById(QUuid::fromString(ids.at(0)))->setName(QStringLiteral("zebra"));
        registry->layoutById(QUuid::fromString(ids.at(1)))->setName(QStringLiteral("Alpha"));
        const QString zebraId = ids.at(0);
        const QString alphaId = ids.at(1);

        StubSettings settings;
        OverlaysPageController c(nullptr, registry, &settings, nullptr);
        c.setShaderOverride(zebraId, QStringLiteral("shared-pack"), {});
        c.setShaderOverride(alphaId, QStringLiteral("shared-pack"), {});

        const QVariantList usages = c.shaderEffectUsages(QStringLiteral("shared-pack"));
        QCOMPARE(usages.size(), 2);
        QCOMPARE(usages.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Alpha"));
        QCOMPARE(usages.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("zebra"));
    }

    void testShaderEffectUsages_orderingAndStaleLabels()
    {
        StubSettings settings;
        OverlaysPageController c(nullptr, nullptr, &settings, nullptr);
        c.setShaderOverride(QString(), QStringLiteral("shared-pack"), {});
        c.setShaderOverride(kLayoutA, QStringLiteral("shared-pack"), {});
        c.setShaderOverride(kLayoutB, QStringLiteral("other-pack"), {});

        const QVariantList usages = c.shaderEffectUsages(QStringLiteral("shared-pack"));
        QCOMPARE(usages.size(), 2);
        // Baseline row first, with an empty path.
        QCOMPARE(usages.first().toMap().value(QStringLiteral("path")).toString(), QString());
        QVERIFY(!usages.first().toMap().value(QStringLiteral("label")).toString().isEmpty());
        // The layout row carries the UUID path and, with no registry to name
        // it, the shared absent-layout label rather than an empty string. An
        // empty label made the consumer fall back to `path` and print all 36
        // characters of the UUID, where the assignments page and the coverage
        // chip both said something readable about the same state.
        const QVariantMap row = usages.last().toMap();
        QCOMPARE(row.value(QStringLiteral("path")).toString(), kLayoutA);
        QCOMPARE(row.value(QStringLiteral("label")).toString(), c.absentLayoutLabel(kLayoutA));
        QVERIFY2(!row.value(QStringLiteral("label")).toString().contains(kLayoutA),
                 "the raw UUID reached the label, so the browser prints it verbatim");

        QVERIFY(c.shaderEffectUsages(QStringLiteral("unused-pack")).isEmpty());
    }

    /// The signal's two arms. A write this controller made names the node it
    /// moved, so a card can skip a refresh it does not need; a write from
    /// anywhere else says only "the whole tree may have moved", because the
    /// settings NOTIFY genuinely does not carry that information.
    void testShaderProfileChanged_refiresFromSettings()
    {
        StubSettings settings;
        OverlaysPageController c(nullptr, nullptr, &settings, nullptr);
        QSignalSpy spy(&c, &OverlaysPageController::shaderProfileChanged);

        c.setShaderOverride(kLayoutA, QStringLiteral("pack"), {});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.constFirst().at(0).toString(), kLayoutA);
        QCOMPARE(spy.constFirst().at(1).toBool(), false);

        // A same-value write is a no-op at the settings layer: no re-fire.
        c.setShaderOverride(kLayoutA, QStringLiteral("pack"), {});
        QCOMPARE(spy.count(), 1);

        // A baseline write names the baseline, whose path IS the empty string
        // — so the empty path alone cannot mean "everything changed", which is
        // what the second argument is for.
        c.setShaderOverride(QString(), QStringLiteral("base-pack"), {});
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), QString());
        QCOMPARE(spy.at(1).at(1).toBool(), false);

        // Clearing an override is a single-node change too.
        QVERIFY(c.clearOverride(kLayoutA));
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(2).at(0).toString(), kLayoutA);
        QCOMPARE(spy.at(2).at(1).toBool(), false);

        // An external settings write (D-Bus, a profile apply, a page reset)
        // reaches the page as a whole-tree change: the write did not come
        // through this controller, so no path can be claimed for it.
        OverlayShaderTree tree = settings.overlayShaderTree();
        tree.setBaseline({QStringLiteral("new-pack"), {}});
        settings.setOverlayShaderTree(tree);
        QCOMPARE(spy.count(), 4);
        QCOMPARE(spy.at(3).at(1).toBool(), true);
    }

    /// nodeState is the one-read form of hasOverride + rawShaderProfile +
    /// resolvedShaderProfile, which a card calls together on every refresh.
    /// It must agree with all three, or a card would see a different tree than
    /// the rest of the page.
    void testNodeState_matchesTheThreeSeparateReads()
    {
        StubSettings settings;
        OverlaysPageController c(nullptr, nullptr, &settings, nullptr);
        OverlayShaderTree tree;
        tree.setBaseline({QStringLiteral("base-pack"), {{QStringLiteral("speed"), 1.0}}});
        tree.setOverride(kLayoutA, {QStringLiteral("pack-a"), {{QStringLiteral("speed"), 2.0}}});
        settings.setOverlayShaderTree(tree);

        // An overridden layout, an inheriting layout, and the baseline.
        for (const QString& path : {kLayoutA, kLayoutB, QString()}) {
            const QVariantMap state = c.nodeState(path);
            QCOMPARE(state.value(QStringLiteral("hasOverride")).toBool(), c.hasOverride(path));
            QCOMPARE(state.value(QStringLiteral("raw")).toMap(), c.rawShaderProfile(path));
            QCOMPARE(state.value(QStringLiteral("resolved")).toMap(), c.resolvedShaderProfile(path));
        }

        // And the values themselves are what the tree says, so a stub that
        // returned three empty maps could not satisfy the loop above.
        const QVariantMap inheriting = c.nodeState(kLayoutB);
        QCOMPARE(inheriting.value(QStringLiteral("hasOverride")).toBool(), false);
        QCOMPARE(inheriting.value(QStringLiteral("resolved")).toMap().value(QStringLiteral("shaderId")).toString(),
                 QStringLiteral("base-pack"));
        QCOMPARE(
            c.nodeState(kLayoutA).value(QStringLiteral("raw")).toMap().value(QStringLiteral("shaderId")).toString(),
            QStringLiteral("pack-a"));
    }
};

QTEST_MAIN(TestOverlaysPageController)
#include "test_overlays_page_controller.moc"
