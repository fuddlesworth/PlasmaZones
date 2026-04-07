// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_support_report.cpp
 * @brief Unit tests for the SupportReport class (core/supportreport.h)
 *
 * Tests verify redaction logic, file reading, sinceMinutes handling,
 * and overall report structure.
 */

#include <QTest>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include "core/supportreport.h"
#include "version.h"

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

    void testRedactHomePath_atEndOfString()
    {
        const QString home = QDir::homePath();
        // Home path at end of string with no trailing slash or path component
        const QString input = QStringLiteral("path is ") + home;
        const QString result = SupportReport::redactHomePath(input);
        QCOMPARE(result, QStringLiteral("path is ~"));
    }

    void testRedactHomePath_noPartialMatch()
    {
        const QString home = QDir::homePath();
        // A longer path that starts with home but continues with a word char
        // e.g. /home/user should not match inside /home/username
        const QString input = home + QStringLiteral("extra/file.txt");
        const QString result = SupportReport::redactHomePath(input);
        // Should NOT be redacted because "extra" is directly appended (no separator)
        QCOMPARE(result, input);
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

    void testGenerate_sinceMinutesZeroUsesDefault()
    {
        // sinceMinutes = 0 should use default (30)
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr, 0);
        QVERIFY(report.contains(QStringLiteral("last 30 minutes")));
    }

    void testGenerate_sinceMinutesNegativeUsesDefault()
    {
        // Negative sinceMinutes should use default (30)
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr, -5);
        QVERIFY(report.contains(QStringLiteral("last 30 minutes")));
    }

    void testGenerate_nullScreenManager()
    {
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr, 30);
        QVERIFY(report.contains(QStringLiteral("screen info unavailable")));
    }

    void testGenerate_nullLayoutManager()
    {
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr, 30);
        QVERIFY(report.contains(QStringLiteral("layout info unavailable")));
    }

    void testGenerate_nullAutotileEngine()
    {
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr, 30);
        QVERIFY(report.contains(QStringLiteral("autotile engine not available")));
    }

    void testGenerate_containsVersionInfo()
    {
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr);
        QVERIFY(report.contains(QStringLiteral("**PlasmaZones:**")));
        QVERIFY(report.contains(VERSION_STRING));
    }

    void testGenerate_containsEnvironmentInfo()
    {
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr);
        QVERIFY(report.contains(QStringLiteral("**Qt:**")));
        QVERIFY(report.contains(QStringLiteral("**OS:**")));
        QVERIFY(report.contains(QStringLiteral("**Kernel:**")));
    }

    void testRedactHomePath_emptyInput()
    {
        QCOMPARE(SupportReport::redactHomePath(QString()), QString());
    }
};

QTEST_MAIN(TestSupportReport)

#include "test_support_report.moc"
