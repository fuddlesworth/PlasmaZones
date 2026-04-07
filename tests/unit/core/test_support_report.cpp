// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_support_report.cpp
 * @brief Unit tests for the SupportReport class (core/supportreport.h)
 *
 * Tests verify redaction logic: home paths are replaced with ~.
 */

#include <QTest>
#include <QDir>

#include "../../../src/core/supportreport.h"

using namespace PlasmaZones;

class TestSupportReport : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testRedactHomePath_replacesHomeDir()
    {
        const QString home = QDir::homePath();
        const QString input = home + QStringLiteral("/Documents/test.txt");
        const QString result = SupportReport::redactHomePath(input);
        QCOMPARE(result, QStringLiteral("~/Documents/test.txt"));
    }

    void testRedactHomePath_noHomeDir_unchanged()
    {
        const QString input = QStringLiteral("/usr/share/something");
        const QString result = SupportReport::redactHomePath(input);
        QCOMPARE(result, input);
    }

    void testRedactHomePath_multipleOccurrences()
    {
        const QString home = QDir::homePath();
        const QString input = home + QStringLiteral("/a ") + home + QStringLiteral("/b");
        const QString result = SupportReport::redactHomePath(input);
        QCOMPARE(result, QStringLiteral("~/a ~/b"));
    }

    void testGenerate_returnsMarkdown()
    {
        // Generate with all nulls — should still produce valid structure
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr, 30);
        QVERIFY(report.contains(QStringLiteral("<details>")));
        QVERIFY(report.contains(QStringLiteral("</details>")));
        QVERIFY(report.contains(QStringLiteral("## Version")));
        QVERIFY(report.contains(QStringLiteral("## Environment")));
        QVERIFY(report.contains(QStringLiteral("## Screens")));
        QVERIFY(report.contains(QStringLiteral("## Config")));
        QVERIFY(report.contains(QStringLiteral("## Layouts")));
        QVERIFY(report.contains(QStringLiteral("## Autotile")));
        QVERIFY(report.contains(QStringLiteral("## Session State")));
        QVERIFY(report.contains(QStringLiteral("## Recent Logs")));
    }

    void testGenerate_sinceMinutesCapped()
    {
        // sinceMinutes > 120 should be capped
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr, 999);
        QVERIFY(report.contains(QStringLiteral("last 120 minutes")));
    }
};

QTEST_MAIN(TestSupportReport)

#include "test_support_report.moc"
