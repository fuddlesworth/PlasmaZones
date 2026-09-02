// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_snappingshaders_page_controller.cpp
 * @brief Reader / mutator tests for SnappingShadersPageController.
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
 *     sorted case-insensitively by label, with an EMPTY label for a layout
 *     the registry cannot name (the browser renders label || path)
 *   - shaderProfileChanged re-fires (with an empty path) from
 *     ISettings::overlayShaderTreeChanged
 *
 * Constructed with null shader and layout registries: the paths exercised
 * here are registry-independent (the registries only feed the pack listing
 * and layout enumeration), and the constructor documents nullptr seams for
 * exactly this use. Settings is StubSettings, whose overlay tree accessors
 * store and emit for real.
 */

#include <QSignalSpy>
#include <QTest>

#include "core/types/overlayshadertree.h"
#include "helpers/StubSettings.h"
#include "settings/pages/snappingshaderspagecontroller.h"

using namespace PlasmaZones;

namespace {
const QString kLayoutA = QStringLiteral("{aaaa0000-0000-0000-0000-000000000000}");
const QString kLayoutB = QStringLiteral("{bbbb0000-0000-0000-0000-000000000000}");
} // namespace

class TestSnappingShadersPageController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testNullSeams_returnEmptyAndNoop()
    {
        SnappingShadersPageController c(nullptr, nullptr, nullptr, nullptr);
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
        SnappingShadersPageController c(nullptr, nullptr, &settings, nullptr);

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
        SnappingShadersPageController c(nullptr, nullptr, &settings, nullptr);
        c.setShaderOverride(kLayoutA, QStringLiteral("pack"), {});

        QVERIFY(!c.clearOverride(QString())); // baseline is rejected
        QVERIFY(c.clearOverride(kLayoutA));
        QVERIFY(!c.hasOverride(kLayoutA));
        QVERIFY(!c.clearOverride(kLayoutA)); // second clear finds nothing
    }

    void testShaderEffectUsages_orderingAndStaleLabels()
    {
        StubSettings settings;
        SnappingShadersPageController c(nullptr, nullptr, &settings, nullptr);
        c.setShaderOverride(QString(), QStringLiteral("shared-pack"), {});
        c.setShaderOverride(kLayoutA, QStringLiteral("shared-pack"), {});
        c.setShaderOverride(kLayoutB, QStringLiteral("other-pack"), {});

        const QVariantList usages = c.shaderEffectUsages(QStringLiteral("shared-pack"));
        QCOMPARE(usages.size(), 2);
        // Baseline row first, with an empty path.
        QCOMPARE(usages.first().toMap().value(QStringLiteral("path")).toString(), QString());
        QVERIFY(!usages.first().toMap().value(QStringLiteral("label")).toString().isEmpty());
        // The layout row carries the UUID path and, with no registry to name
        // it, an EMPTY label — the consumer's label || path fallback shows
        // the UUID.
        const QVariantMap row = usages.last().toMap();
        QCOMPARE(row.value(QStringLiteral("path")).toString(), kLayoutA);
        QCOMPARE(row.value(QStringLiteral("label")).toString(), QString());

        QVERIFY(c.shaderEffectUsages(QStringLiteral("unused-pack")).isEmpty());
    }

    void testShaderProfileChanged_refiresFromSettings()
    {
        StubSettings settings;
        SnappingShadersPageController c(nullptr, nullptr, &settings, nullptr);
        QSignalSpy spy(&c, &SnappingShadersPageController::shaderProfileChanged);

        c.setShaderOverride(kLayoutA, QStringLiteral("pack"), {});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.constFirst().constFirst().toString(), QString()); // always full-refresh

        // A same-value write is a no-op at the settings layer: no re-fire.
        c.setShaderOverride(kLayoutA, QStringLiteral("pack"), {});
        QCOMPARE(spy.count(), 1);

        // An external settings write (D-Bus, reload) reaches the page too.
        OverlayShaderTree tree = settings.overlayShaderTree();
        tree.setBaseline({QStringLiteral("new-pack"), {}});
        settings.setOverlayShaderTree(tree);
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_MAIN(TestSnappingShadersPageController)
#include "test_snappingshaders_page_controller.moc"
