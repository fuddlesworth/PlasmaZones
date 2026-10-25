// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/MatchTypes.h>

#include <QTest>

using namespace PhosphorRules;

class TestMatchTypes : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testFieldRoundTrip_data()
    {
        // Canary: the loop bound is derived from FieldCount, not hard-coded.
        // If this fails, an enumerator was added/removed without updating
        // FieldCount in MatchTypes.h.
        QCOMPARE(FieldCount, 44);
        QTest::addColumn<int>("fieldValue");
        for (int v = 0; v < FieldCount; ++v) {
            QTest::addRow("field-%d", v) << v;
        }
    }

    void testFieldRoundTrip()
    {
        QFETCH(int, fieldValue);
        const Field field = static_cast<Field>(fieldValue);
        const QString s = fieldToString(field);
        const auto parsed = fieldFromString(s);
        QVERIFY(parsed.has_value());
        QCOMPARE(static_cast<int>(*parsed), fieldValue);
    }

    void testOperatorRoundTrip_data()
    {
        // Canary: the loop bound is derived from OperatorCount — see
        // testFieldRoundTrip_data.
        QCOMPARE(OperatorCount, 8);
        QTest::addColumn<int>("opValue");
        for (int v = 0; v < OperatorCount; ++v) {
            QTest::addRow("op-%d", v) << v;
        }
    }

    void testOperatorRoundTrip()
    {
        QFETCH(int, opValue);
        const Operator op = static_cast<Operator>(opValue);
        const QString s = operatorToString(op);
        const auto parsed = operatorFromString(s);
        QVERIFY(parsed.has_value());
        QCOMPARE(static_cast<int>(*parsed), opValue);
    }

    void testFieldFromString_strict()
    {
        QVERIFY(!fieldFromString(QStringLiteral("notAField")).has_value());
        QVERIFY(!fieldFromString(QString()).has_value());
        // Case-insensitive.
        QCOMPARE(fieldFromString(QStringLiteral("APPID")), Field::AppId);
    }

    void testOperatorFromString_strict()
    {
        QVERIFY(!operatorFromString(QStringLiteral("notAnOp")).has_value());
        QCOMPARE(operatorFromString(QStringLiteral("REGEX")), Operator::Regex);
    }

    void testFieldClassification_data()
    {
        QTest::addColumn<int>("fieldValue");
        for (int v = 0; v < FieldCount; ++v) {
            QTest::addRow("field-%d", v) << v;
        }
    }

    void testFieldClassification()
    {
        // Every Field must fall into EXACTLY ONE of the three value-kind
        // classifications (string / numeric / bool). WindowType is the one
        // enum-valued field — it deliberately belongs to none, so the
        // expected count is one classification per field except WindowType
        // which has zero. Data-driving over all FieldCount enumerators
        // catches a new field that forgets a classification, or one that
        // accidentally lands in two.
        QFETCH(int, fieldValue);
        const Field field = static_cast<Field>(fieldValue);
        const int classifications =
            (fieldIsString(field) ? 1 : 0) + (fieldIsNumeric(field) ? 1 : 0) + (fieldIsBool(field) ? 1 : 0);
        if (field == Field::WindowType) {
            // WindowType is enum-valued — none of the three value kinds.
            QCOMPARE(classifications, 0);
        } else {
            QCOMPARE(classifications, 1);
        }
    }

    void testFieldIsContext()
    {
        // Exactly the eleven context fields are context (screen / desktop /
        // activity, the placement Mode, the tiled-window count, the screen
        // orientation, the active layout, the system colour scheme, and the
        // screen width, height and aspect ratio) —
        // everything else is a window property. Action/match compatibility hinges on this split, so pin it
        // down explicitly rather than re-deriving it from
        // fieldIsString/fieldIsBool/fieldIsNumeric.
        QVERIFY(fieldIsContext(Field::ScreenId));
        QVERIFY(fieldIsContext(Field::VirtualDesktop));
        QVERIFY(fieldIsContext(Field::Activity));
        // Mode is the placement-mode context field — present during context
        // resolution so a per-mode gap/appearance rule participates in the
        // cascade.
        QVERIFY(fieldIsContext(Field::Mode));
        // TiledWindowCount is a context field (the tiled count for the
        // screen+desktop being resolved) so a SetTilingAlgorithm rule can key
        // on it during windowless context resolution.
        QVERIFY(fieldIsContext(Field::TiledWindowCount));
        QVERIFY(fieldIsNumeric(Field::TiledWindowCount));
        // ScreenOrientation and ActiveLayout are geometry/layout context fields,
        // stamped during windowless context resolution so an orientation- or
        // layout-keyed gap/overlay/assignment rule participates in the cascade.
        QVERIFY(fieldIsContext(Field::ScreenOrientation));
        QVERIFY(fieldIsContext(Field::ActiveLayout));
        // ColorScheme is the session-wide light/dark context field, stamped
        // during windowless context resolution AND on daemon window queries.
        QVERIFY(fieldIsContext(Field::ColorScheme));
        // The screen-size fields are context too, so a resolution-keyed rule can
        // drive the assignment cascade. Width and height are numeric (the Int
        // operators), the aspect ratio a string token.
        QVERIFY(fieldIsContext(Field::ScreenWidth));
        QVERIFY(fieldIsContext(Field::ScreenHeight));
        QVERIFY(fieldIsContext(Field::ScreenAspectRatio));
        QVERIFY(fieldIsNumeric(Field::ScreenWidth));
        QVERIFY(fieldIsNumeric(Field::ScreenHeight));
        QVERIFY(fieldIsString(Field::ScreenAspectRatio));

        // The capability fields added alongside them are window properties, not
        // context, so a rule keying on them is a per-window match.
        QVERIFY(!fieldIsContext(Field::IsMovable));
        QVERIFY(!fieldIsContext(Field::IsMaximizable));
        QVERIFY(!fieldIsContext(Field::AppId));
        QVERIFY(!fieldIsContext(Field::WindowClass));
        QVERIFY(!fieldIsContext(Field::DesktopFile));
        QVERIFY(!fieldIsContext(Field::WindowRole));
        QVERIFY(!fieldIsContext(Field::Pid));
        QVERIFY(!fieldIsContext(Field::Title));
        QVERIFY(!fieldIsContext(Field::WindowType));
        QVERIFY(!fieldIsContext(Field::IsSticky));
        QVERIFY(!fieldIsContext(Field::IsFullscreen));
        QVERIFY(!fieldIsContext(Field::IsMinimized));
        QVERIFY(!fieldIsContext(Field::IsMaximized));
        QVERIFY(!fieldIsContext(Field::IsFocused));
        QVERIFY(!fieldIsContext(Field::IsTransient));
        QVERIFY(!fieldIsContext(Field::IsNotification));
        QVERIFY(!fieldIsContext(Field::Width));
        QVERIFY(!fieldIsContext(Field::Height));
        QVERIFY(!fieldIsContext(Field::KeepAbove));
        QVERIFY(!fieldIsContext(Field::KeepBelow));
        QVERIFY(!fieldIsContext(Field::SkipTaskbar));
        QVERIFY(!fieldIsContext(Field::SkipPager));
        QVERIFY(!fieldIsContext(Field::SkipSwitcher));
        QVERIFY(!fieldIsContext(Field::IsModal));
        QVERIFY(!fieldIsContext(Field::HasDecoration));
        QVERIFY(!fieldIsContext(Field::IsResizable));
        QVERIFY(!fieldIsContext(Field::PositionX));
        QVERIFY(!fieldIsContext(Field::PositionY));
        QVERIFY(!fieldIsContext(Field::CaptionNormal));
        QVERIFY(!fieldIsContext(Field::IsFloating));
        QVERIFY(!fieldIsContext(Field::IsSnapped));
        QVERIFY(!fieldIsContext(Field::Zone));
        QVERIFY(!fieldIsContext(Field::IsTiled));
    }

    void testModeTokenVocabulary()
    {
        // The Field::Mode vocabulary is a WIRE contract: the tokens are written
        // into saved rules and stamped onto every context query by producers in
        // other libraries. Nothing validates a Mode leaf's value at load (the
        // match language has no per-field vocabulary check), so a renamed
        // constant would not fail anything downstream — it would just stop
        // matching. Spell the three wire strings out literally here, the one
        // place they are pinned, so a rename has to come through this test.
        QCOMPARE(QString(ModeToken::Snapping), QStringLiteral("snapping"));
        QCOMPARE(QString(ModeToken::Tiling), QStringLiteral("tiling"));
        QCOMPARE(QString(ModeToken::Scrolling), QStringLiteral("scrolling"));

        // The enumeration is complete and in the documented order — a caller
        // validating a hand-edited value iterates it.
        QCOMPARE(static_cast<int>(sizeof(kModeTokens) / sizeof(kModeTokens[0])), 3);
        QCOMPARE(QString(kModeTokens[0]), QString(ModeToken::Snapping));
        QCOMPARE(QString(kModeTokens[1]), QString(ModeToken::Tiling));
        QCOMPARE(QString(kModeTokens[2]), QString(ModeToken::Scrolling));

        // The trap this vocabulary exists to prevent: the engine-mode ACTION
        // vocabulary (SetEngineMode / DisableEngine, in the private
        // ruleaction_builtins_p.h) spells the middle value "autotile". A Mode
        // match leaf carrying that token silently never matches, so the two
        // must stay distinct. Asserted against the literal deliberately — the
        // point is that the action spelling is NOT reachable from here.
        QVERIFY(QString(ModeToken::Tiling) != QStringLiteral("autotile"));
        for (const QLatin1StringView token : kModeTokens) {
            QVERIFY2(QString(token) != QStringLiteral("autotile"), token.data());
        }
    }

    void testAspectRatioToken_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<QString>("token");
        // The resolutions the vocabulary exists for, each under the name the
        // monitor is sold as. The ultrawide family is not exactly 21:9, and
        // all of it must still land there rather than on 16:9 or 32:10.
        QTest::addRow("5120x1440 super-ultrawide") << 5120 << 1440 << QStringLiteral("32:9");
        QTest::addRow("2560x1440 its KVM half") << 2560 << 1440 << QStringLiteral("16:9");
        QTest::addRow("1920x1080") << 1920 << 1080 << QStringLiteral("16:9");
        QTest::addRow("1366x768 off by a pixel") << 1366 << 768 << QStringLiteral("16:9");
        QTest::addRow("3440x1440 ultrawide") << 3440 << 1440 << QStringLiteral("21:9");
        QTest::addRow("2560x1080 ultrawide") << 2560 << 1080 << QStringLiteral("21:9");
        QTest::addRow("3840x1600 ultrawide") << 3840 << 1600 << QStringLiteral("21:9");
        QTest::addRow("3840x1200 dual 16:10") << 3840 << 1200 << QStringLiteral("32:10");
        QTest::addRow("1920x1200") << 1920 << 1200 << QStringLiteral("16:10");
        QTest::addRow("2256x1504") << 2256 << 1504 << QStringLiteral("3:2");
        QTest::addRow("1600x1200") << 1600 << 1200 << QStringLiteral("4:3");
        QTest::addRow("1280x1024") << 1280 << 1024 << QStringLiteral("5:4");
        QTest::addRow("square") << 1920 << 1920 << QStringLiteral("1:1");
        QTest::addRow("1080x1920 rotated") << 1080 << 1920 << QStringLiteral("9:16");
        QTest::addRow("1200x1920 rotated 16:10") << 1200 << 1920 << QStringLiteral("10:16");
        QTest::addRow("1440x3440 rotated ultrawide") << 1440 << 3440 << QStringLiteral("9:21");
        QTest::addRow("zero width") << 0 << 1080 << QString();
        QTest::addRow("negative height") << 1920 << -1 << QString();
    }

    void testAspectRatioToken()
    {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(QString, token);
        QCOMPARE(aspectRatioTokenFor(width, height), token);
    }

    void testAspectRatioVocabularyIsUnique()
    {
        // The picker lists the vocabulary verbatim, so a duplicate token would
        // show twice and a duplicate ratio would make the nearest-match order
        // decide between two names for one shape.
        QSet<QString> tokens;
        QSet<double> ratios;
        for (const AspectRatioToken& entry : kAspectRatioTokens) {
            QVERIFY2(!tokens.contains(QString(entry.token)), entry.token.data());
            QVERIFY2(!ratios.contains(entry.ratio), entry.token.data());
            tokens.insert(QString(entry.token));
            ratios.insert(entry.ratio);
            // Each token names a ratio it maps back to.
            QCOMPARE(aspectRatioTokenFor(qRound(entry.ratio * 1000.0), 1000), QString(entry.token));
        }
    }

    void testFieldTableOrdering()
    {
        for (int i = 0; i < FieldCount; ++i) {
            QCOMPARE(static_cast<int>(kFieldTable[i].field), i);
        }
    }

    void testFieldIsContext_coversAllFields_data()
    {
        // Canary — every Field must answer the context question. Data-driving
        // over FieldCount catches a new enumerator added without classifying it.
        QTest::addColumn<int>("fieldValue");
        for (int v = 0; v < FieldCount; ++v) {
            QTest::addRow("field-%d", v) << v;
        }
    }

    void testFieldIsContext_coversAllFields()
    {
        QFETCH(int, fieldValue);
        const Field field = static_cast<Field>(fieldValue);
        // The classifier must agree with the authoritative table source for every
        // field. This catches a new enumerator whose `fieldIsContext` answer
        // diverges from its `kFieldTable` FieldSource (the canary the data-driven
        // shape exists for).
        QCOMPARE(fieldIsContext(field), kFieldTable[fieldValue].source == FieldSource::Context);
    }
};

QTEST_GUILESS_MAIN(TestMatchTypes)
#include "test_matchtypes.moc"
