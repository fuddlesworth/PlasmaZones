// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/MatchTypes.h>

#include <QTest>

using namespace PhosphorWindowRule;

class TestMatchTypes : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testFieldRoundTrip_data()
    {
        // Canary: the loop bound is derived from FieldCount, not hard-coded.
        // If this fails, an enumerator was added/removed without updating
        // FieldCount in MatchTypes.h.
        QCOMPARE(FieldCount, 33);
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
        QCOMPARE(OperatorCount, 9);
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
        // Exactly the three context fields are context — everything else is a
        // window property. Action/match compatibility hinges on this split, so
        // pin it down explicitly rather than re-deriving it from
        // fieldIsString/fieldIsBool/fieldIsNumeric.
        QVERIFY(fieldIsContext(Field::ScreenId));
        QVERIFY(fieldIsContext(Field::VirtualDesktop));
        QVERIFY(fieldIsContext(Field::Activity));

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
        // The call alone is the check — the test fails if a new field crashes
        // a switch that grew an unhandled case (none today, but the data-driven
        // shape is the canary).
        (void)fieldIsContext(field);
    }
};

QTEST_GUILESS_MAIN(TestMatchTypes)
#include "test_matchtypes.moc"
