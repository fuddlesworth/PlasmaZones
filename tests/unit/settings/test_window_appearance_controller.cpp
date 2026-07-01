// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_appearance_controller.cpp
 * @brief Round-trip tests for the "Apply to" scope selector helpers on
 *        @c WindowAppearanceController (the border / title-bar baseline scope).
 *
 * The Appearance page's scope picker translates between a scope token and a
 * rule's `match` JSON via matchJsonForScope() / scopeOfMatch(). These pin:
 *   1. Each preset token round-trips (token -> match JSON -> token).
 *   2. The "tiled" preset matches the daemon's seeded default
 *      (ConfigDefaults::tiledAndSnappedScopeMatch) so the UI and the seeder
 *      never drift.
 *   3. An empty / catch-all match classifies as "all", and a recognized but
 *      non-preset expression classifies as "custom" rather than being coerced.
 */

#include <QTest>

#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>

#include "config/configdefaults.h"
#include "settings/windowappearancecontroller.h"

using namespace PlasmaZones;
using PhosphorRules::Field;
using PhosphorRules::MatchExpression;
using PhosphorRules::Operator;

class TestWindowAppearanceController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scopeTokensRoundTrip_data()
    {
        QTest::addColumn<QString>("scope");
        QTest::newRow("tiled") << QStringLiteral("tiled");
        QTest::newRow("normal") << QStringLiteral("normal");
        QTest::newRow("all") << QStringLiteral("all");
    }

    void scopeTokensRoundTrip()
    {
        QFETCH(QString, scope);
        WindowAppearanceController controller;
        const QJsonObject match = controller.matchJsonForScope(scope);
        QCOMPARE(controller.scopeOfMatch(match), scope);
    }

    void tiledPresetMatchesSeededDefault()
    {
        WindowAppearanceController controller;
        // The UI preset and the daemon's fresh-install seed must be byte-identical
        // so a freshly seeded baseline reads back as "tiled" in the picker.
        QCOMPARE(controller.matchJsonForScope(QStringLiteral("tiled")),
                 ConfigDefaults::tiledAndSnappedScopeMatch().toJson());
    }

    void unknownTokenIsCatchAll()
    {
        WindowAppearanceController controller;
        // A bad token must never narrow the baseline — it falls back to the
        // catch-all, which classifies as "all".
        const QJsonObject match = controller.matchJsonForScope(QStringLiteral("bogus"));
        QCOMPARE(controller.scopeOfMatch(match), QStringLiteral("all"));
    }

    void emptyMatchIsAll()
    {
        WindowAppearanceController controller;
        // An absent / empty match object (the QML fallback for a rule with no
        // stored match) is the catch-all "all" scope.
        QCOMPARE(controller.scopeOfMatch(QJsonObject{}), QStringLiteral("all"));
        QCOMPARE(controller.scopeOfMatch(MatchExpression{}.toJson()), QStringLiteral("all"));
    }

    void nonPresetExpressionIsCustom()
    {
        WindowAppearanceController controller;
        // A valid match that is not one of the presets (e.g. a single AppId leaf
        // authored on the Rules page) is reported as custom, not coerced.
        const QJsonObject custom =
            MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QVariant(QStringLiteral("org.kde.konsole")))
                .toJson();
        QCOMPARE(controller.scopeOfMatch(custom), QStringLiteral("custom"));
    }

    void normalPresetExcludesTransients()
    {
        WindowAppearanceController controller;
        const QJsonObject match = controller.matchJsonForScope(QStringLiteral("normal"));
        const auto parsed = MatchExpression::fromJson(match);
        QVERIFY(parsed.has_value());
        // WindowType is encoded as the underlying int, not the wire token — a
        // leaf carrying "normal" as a string would compare as 0 (Unknown).
        const QJsonObject expected =
            MatchExpression::makeAll(
                {MatchExpression::makeLeaf(Field::WindowType, Operator::Equals,
                                           QVariant(static_cast<int>(PhosphorProtocol::WindowType::Normal))),
                 MatchExpression::makeLeaf(Field::IsTransient, Operator::Equals, QVariant(false))})
                .toJson();
        QCOMPARE(match, expected);
    }
};

QTEST_MAIN(TestWindowAppearanceController)
#include "test_window_appearance_controller.moc"
