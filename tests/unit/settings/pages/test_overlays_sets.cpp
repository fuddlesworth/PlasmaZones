// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_overlays_sets.cpp
 * @brief Overlay sets — the OverlaysPageController domain over ShaderSetStore.
 *
 * The shared store's own file machinery is covered by the decoration and motion
 * suites. What is pinned here is the part that is specific to this domain, and
 * every case corresponds to a bug the motion-set rewrite had to fix:
 *
 *   - a set round-trips: what save captured, apply restores;
 *   - an override whose shaderId is EMPTY survives the round trip. That node
 *     means "suppress the global default on this layout", so pruning it as
 *     empty would silently drop a real choice — the shape that dropped an
 *     explicit empty parameter map from motion sets;
 *   - a layout the machine does not have is SKIPPED, not refused, because
 *     layout ids are per-computer and refusing would make shared sets useless;
 *   - and the active badge ignores those skipped entries, or a set would read
 *     dark forever over content it could never have applied — the shape where
 *     one field a set did not own kept the whole set dark;
 *   - but a set of ONLY absent layouts is not active, or it would light up on a
 *     machine that applied none of it;
 *   - apply is ONE settings write, so a failure is honestly all-or-nothing and
 *     the cards refresh once rather than once per layout.
 *
 * NOT covered here: the validator's refusal of a set naming a shader pack this
 * build does not have. The fixture passes a null shader registry (the ctor's
 * documented seam), and that arm short-circuits without one; covering it needs
 * a real ShaderRegistry with a pack directory behind it, which is a fixture
 * this suite would otherwise have no use for.
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "core/types/overlayshadertree.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/SetRowHelpers.h"
#include "helpers/StubSettings.h"
#include "settings/pages/overlayspagecontroller.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kSetName = QStringLiteral("Test Set");
const QString kAbsentLayout = QStringLiteral("{cccc0000-0000-0000-0000-000000000000}");
} // namespace

class TestOverlaySets : public QObject
{
    Q_OBJECT

private:
    // Owned per-test, in construction order: the registry outlives the
    // controller that reads it.
    std::unique_ptr<QTemporaryDir> m_setsDir;
    std::unique_ptr<StubSettings> m_settings;
    std::unique_ptr<PhosphorZones::LayoutRegistry> m_layouts;
    std::unique_ptr<OverlaysPageController> m_controller;
    QString m_layoutA;

    /// [[nodiscard]] bool rather than QVERIFY-in-void: a QVERIFY failure in a
    /// void helper returns from the HELPER only, so the calling slot would
    /// carry on and dereference the members this never got round to building,
    /// crashing the suite instead of failing it. Same conversion the sibling
    /// relocation tests already made. Call it as QVERIFY(makeFixture()).
    [[nodiscard]] bool makeFixture()
    {
        // Tear the previous controller down FIRST. It borrows the settings and
        // the layout registry, and the reassignments below destroy those, so
        // reassigning it last would leave it outliving everything it holds —
        // the inverse of the ownership order the member comment above declares.
        m_controller.reset();

        m_setsDir = std::make_unique<QTemporaryDir>();
        if (!m_setsDir->isValid()) {
            return false;
        }
        m_settings = std::make_unique<StubSettings>();
        m_layouts.reset(TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        auto* layout = new PhosphorZones::Layout(m_layouts.get());
        layout->setName(QStringLiteral("Layout A"));
        m_layouts->addLayout(layout);
        m_layoutA = layout->id().toString();

        m_controller = std::make_unique<OverlaysPageController>(nullptr, m_layouts.get(), m_settings.get(), nullptr);
        // ctest runs suites in parallel against one qttest data dir, so the
        // store must not touch the shared location.
        m_controller->setSetsDirOverride(m_setsDir->path());
        return true;
    }

    ShaderSetStore* sets() const
    {
        return m_controller->setsBridge();
    }

private Q_SLOTS:
    void testRoundTrip_globalAndOverrideSurviveSaveAndApply()
    {
        IsolatedConfigGuard guard;
        QVERIFY(makeFixture());

        OverlayShaderTree tree;
        tree.setBaseline({QStringLiteral("cosmic-flow"), {{QStringLiteral("speed"), 1.5}}});
        tree.setOverride(m_layoutA, {QStringLiteral("neon-city"), {}});
        m_settings->setOverlayShaderTree(tree);

        QVERIFY(sets()->saveCurrentAsSet(kSetName, QString(), false));

        // Move live state away, then apply the set back.
        OverlayShaderTree other;
        other.setBaseline({QStringLiteral("something-else"), {}});
        m_settings->setOverlayShaderTree(other);
        QVERIFY(sets()->applySet(kSetName));

        const OverlayShaderTree restored = m_settings->overlayShaderTree();
        QCOMPARE(restored.baseline().shaderId, QStringLiteral("cosmic-flow"));
        QCOMPARE(restored.baseline().parameters.value(QStringLiteral("speed")).toDouble(), 1.5);
        QCOMPARE(restored.directOverride(m_layoutA).shaderId, QStringLiteral("neon-city"));
    }

    /// The empty-shaderId override is a REAL state: it suppresses the global
    /// default for that layout. A snapshot that pruned "empty" profiles would
    /// drop it, and the user's "no overlay here" would vanish from every set.
    void testSuppressingOverrideIsCapturedNotPrunedAsEmpty()
    {
        IsolatedConfigGuard guard;
        QVERIFY(makeFixture());

        OverlayShaderTree tree;
        tree.setBaseline({QStringLiteral("cosmic-flow"), {}});
        tree.setOverride(m_layoutA, {}); // present, empty: "no overlay on Layout A"
        m_settings->setOverlayShaderTree(tree);
        QVERIFY(sets()->saveCurrentAsSet(kSetName, QString(), false));

        // Give the layout an overlay, then apply the set: the suppression must
        // come back, not be left alone as if the set never mentioned it.
        tree.setOverride(m_layoutA, {QStringLiteral("neon-city"), {}});
        m_settings->setOverlayShaderTree(tree);
        QVERIFY(sets()->applySet(kSetName));

        const OverlayShaderTree restored = m_settings->overlayShaderTree();
        QVERIFY2(restored.hasOverride(m_layoutA), "the suppressing override was pruned from the set");
        QVERIFY(restored.directOverride(m_layoutA).shaderId.isEmpty());
        QVERIFY2(restored.resolve(m_layoutA).shaderId.isEmpty(),
                 "the layout resolved through to the baseline, so suppression was lost");
    }

    /// A layout id is per-computer. A set from another machine names ids this
    /// one has never seen, so the entries that CAN apply must still apply.
    void testAbsentLayoutIsSkippedNotRefused()
    {
        IsolatedConfigGuard guard;
        QVERIFY(makeFixture());

        OverlayShaderTree tree;
        tree.setBaseline({QStringLiteral("cosmic-flow"), {}});
        tree.setOverride(m_layoutA, {QStringLiteral("neon-city"), {}});
        tree.setOverride(kAbsentLayout, {QStringLiteral("neon-city"), {}});
        m_settings->setOverlayShaderTree(tree);
        QVERIFY(sets()->saveCurrentAsSet(kSetName, QString(), false));

        m_settings->setOverlayShaderTree(OverlayShaderTree{});
        QVERIFY2(sets()->applySet(kSetName), "a set naming an absent layout was refused whole");

        const OverlayShaderTree restored = m_settings->overlayShaderTree();
        QCOMPARE(restored.baseline().shaderId, QStringLiteral("cosmic-flow"));
        QCOMPARE(restored.directOverride(m_layoutA).shaderId, QStringLiteral("neon-city"));
        QVERIFY2(!restored.hasOverride(kAbsentLayout), "an entry for a layout not on this machine was written anyway");
    }

    /// ...and the badge must not be held dark by the entry apply skipped.
    void testActiveBadgeIgnoresEntriesApplyCannotWrite()
    {
        IsolatedConfigGuard guard;
        QVERIFY(makeFixture());

        OverlayShaderTree tree;
        tree.setBaseline({QStringLiteral("cosmic-flow"), {}});
        tree.setOverride(m_layoutA, {QStringLiteral("neon-city"), {}});
        tree.setOverride(kAbsentLayout, {QStringLiteral("neon-city"), {}});
        m_settings->setOverlayShaderTree(tree);
        QVERIFY(sets()->saveCurrentAsSet(kSetName, QString(), false));

        // Drop the absent layout from live state — apply could never write it.
        tree.clearOverride(kAbsentLayout);
        m_settings->setOverlayShaderTree(tree);

        const QVariantMap row = rowFor(sets(), kSetName);
        QVERIFY(!row.isEmpty());
        QVERIFY2(row.value(QStringLiteral("active")).toBool(),
                 "the set read inactive because of an entry this machine can never apply");
    }

    /// But a set that is ENTIRELY absent layouts applied nothing here, so it
    /// must not claim to be active.
    void testSetOfOnlyAbsentLayoutsIsNeverActive()
    {
        IsolatedConfigGuard guard;
        QVERIFY(makeFixture());

        OverlayShaderTree tree;
        tree.setOverride(m_layoutA, {QStringLiteral("neon-city"), {}});
        m_settings->setOverlayShaderTree(tree);
        QVERIFY(sets()->saveCurrentAsSet(kSetName, QString(), false));

        // Remove the only layout the set covers: every entry is now
        // inapplicable, and the set describes nothing that is live here.
        auto* toRemove = m_layouts->layoutById(QUuid::fromString(m_layoutA));
        QVERIFY2(toRemove, "fixture layout vanished before the removal this slot depends on");
        m_layouts->removeLayout(toRemove);

        const QVariantMap row = rowFor(sets(), kSetName);
        QVERIFY(!row.isEmpty());
        QVERIFY2(!row.value(QStringLiteral("active")).toBool(),
                 "a set covering no layout on this machine reported itself active");
    }

    /// Apply must be ONE settings write. Per-entry writes would emit the tree
    /// NOTIFY per entry, refreshing every card and re-running the badge sweep
    /// against each intermediate tree — and would make a failure half-applied.
    void testApplyIsASingleSettingsWrite()
    {
        IsolatedConfigGuard guard;
        QVERIFY(makeFixture());

        OverlayShaderTree tree;
        tree.setBaseline({QStringLiteral("cosmic-flow"), {}});
        tree.setOverride(m_layoutA, {QStringLiteral("neon-city"), {}});
        m_settings->setOverlayShaderTree(tree);
        QVERIFY(sets()->saveCurrentAsSet(kSetName, QString(), false));
        m_settings->setOverlayShaderTree(OverlayShaderTree{});

        QSignalSpy spy(m_settings.get(), &ISettings::overlayShaderTreeChanged);
        QVERIFY(sets()->applySet(kSetName));
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestOverlaySets)
#include "test_overlays_sets.moc"
