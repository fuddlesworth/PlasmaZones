// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_geometry_provenance.cpp
 * @brief Unit tests for the GapLayer provenance out-parameter of
 *        PlasmaZones::GeometryUtils::getEffectiveZonePadding / getEffectiveOuterGaps.
 *
 * The provenance query that powers the settings "what wins here?" inspector
 * reads the SAME resolution path that geometry uses, so these assert that the
 * reported winning layer matches the value the cascade actually returns. The
 * context-rule, global, and default layers are drivable with the rigid
 * StubSettings; the per-screen and layout layers need a richer fixture and are
 * covered by the daemon-side integration path.
 */

#include <QTest>
#include <QVariantMap>

#include <PhosphorEngine/IGeometrySettings.h>

#include "../helpers/StubSettings.h"
#include "core/geometryutils.h"

using PlasmaZones::StubSettings;
using PlasmaZones::GeometryUtils::GapLayer;
using PlasmaZones::GeometryUtils::getEffectiveOuterGaps;
using PlasmaZones::GeometryUtils::getEffectiveZonePadding;
namespace PSK = PhosphorEngine::PerScreenSnappingKey;

class TestGeometryProvenance : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Zone padding ────────────────────────────────────────────────────

    void test_zonePadding_contextRuleWins()
    {
        QVariantMap ruleOverride;
        ruleOverride.insert(PSK::ZonePadding, 23);

        GapLayer layer = GapLayer::Default;
        const int value = getEffectiveZonePadding(nullptr, nullptr, QString(), ruleOverride, &layer);

        QCOMPARE(value, 23);
        QCOMPARE(layer, GapLayer::ContextRule);
    }

    void test_zonePadding_globalWins()
    {
        StubSettings settings;

        GapLayer layer = GapLayer::Default;
        const int value = getEffectiveZonePadding(nullptr, &settings, QString(), {}, &layer);

        QCOMPARE(value, settings.zonePadding());
        QCOMPARE(layer, GapLayer::Global);
    }

    void test_zonePadding_defaultWins()
    {
        GapLayer layer = GapLayer::Global;
        const int value = getEffectiveZonePadding(nullptr, nullptr, QString(), {}, &layer);

        QCOMPARE(value, PhosphorEngine::GeometryDefaults::ZonePadding);
        QCOMPARE(layer, GapLayer::Default);
    }

    void test_zonePadding_nullOutParamIsSafe()
    {
        StubSettings settings;
        // Must not crash when the caller does not want provenance (the existing
        // geometry call sites pass nothing).
        QCOMPARE(getEffectiveZonePadding(nullptr, &settings), settings.zonePadding());
    }

    // ─── Outer gaps ──────────────────────────────────────────────────────

    void test_outerGaps_contextRuleWins()
    {
        QVariantMap ruleOverride;
        ruleOverride.insert(PSK::OuterGap, 17);

        GapLayer layer = GapLayer::Default;
        const auto gaps = getEffectiveOuterGaps(nullptr, nullptr, QString(), ruleOverride, &layer);

        QCOMPARE(gaps.top, 17);
        QCOMPARE(layer, GapLayer::ContextRule);
    }

    void test_outerGaps_globalWins()
    {
        StubSettings settings;

        GapLayer layer = GapLayer::Default;
        const auto gaps = getEffectiveOuterGaps(nullptr, &settings, QString(), {}, &layer);

        QCOMPARE(gaps.top, settings.outerGap());
        QCOMPARE(layer, GapLayer::Global);
    }

    void test_outerGaps_defaultWins()
    {
        GapLayer layer = GapLayer::Global;
        const auto gaps = getEffectiveOuterGaps(nullptr, nullptr, QString(), {}, &layer);

        QCOMPARE(gaps.top, PhosphorEngine::GeometryDefaults::OuterGap);
        QCOMPARE(layer, GapLayer::Default);
    }
};

QTEST_MAIN(TestGeometryProvenance)
#include "test_geometry_provenance.moc"
