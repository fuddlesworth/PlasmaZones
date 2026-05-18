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
        QCOMPARE(FieldCount, 13);
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

    void testFieldClassification()
    {
        QVERIFY(fieldIsString(Field::AppId));
        QVERIFY(fieldIsString(Field::Title));
        QVERIFY(fieldIsString(Field::ScreenId));
        QVERIFY(!fieldIsString(Field::Pid));
        QVERIFY(!fieldIsString(Field::WindowType));

        QVERIFY(fieldIsNumeric(Field::Pid));
        QVERIFY(fieldIsNumeric(Field::VirtualDesktop));
        QVERIFY(!fieldIsNumeric(Field::AppId));

        QVERIFY(fieldIsBool(Field::IsSticky));
        QVERIFY(fieldIsBool(Field::IsFullscreen));
        QVERIFY(fieldIsBool(Field::IsMinimized));
        QVERIFY(!fieldIsBool(Field::WindowType));
    }
};

QTEST_MAIN(TestMatchTypes)
#include "test_matchtypes.moc"
