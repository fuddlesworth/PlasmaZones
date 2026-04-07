// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_support_report.cpp
 * @brief Unit tests for the SupportReport class (core/supportreport.h)
 *
 * Tests verify redaction logic: home paths are replaced, window classes
 * are hashed, and window titles are stripped.
 */

#include <QTest>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>

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

    void testRedactSessionJson_hashesWindowClass()
    {
        QJsonObject entry;
        entry[QLatin1String("windowClass")] = QStringLiteral("org.kde.dolphin");
        entry[QLatin1String("windowTitle")] = QStringLiteral("Home - Dolphin");
        entry[QLatin1String("zoneId")] = QStringLiteral("{abc-123}");

        QJsonObject root;
        QJsonArray windows;
        windows.append(entry);
        root[QLatin1String("windows")] = windows;

        const QString input = QString::fromUtf8(QJsonDocument(root).toJson());
        const QString result = SupportReport::redactSessionJson(input);

        // Parse result
        QJsonDocument resultDoc = QJsonDocument::fromJson(result.toUtf8());
        QVERIFY(resultDoc.isObject());
        QJsonArray resultWindows = resultDoc.object().value(QLatin1String("windows")).toArray();
        QCOMPARE(resultWindows.size(), 1);

        QJsonObject resultEntry = resultWindows[0].toObject();

        // windowClass should be hashed (8 hex chars)
        const QString hashedClass = resultEntry.value(QLatin1String("windowClass")).toString();
        QCOMPARE(hashedClass.length(), 8);

        // Verify it's the correct hash
        const QByteArray expectedHash =
            QCryptographicHash::hash(QStringLiteral("org.kde.dolphin").toUtf8(), QCryptographicHash::Sha256)
                .toHex()
                .left(8);
        QCOMPARE(hashedClass, QString::fromLatin1(expectedHash));

        // windowTitle should be removed
        QVERIFY(!resultEntry.contains(QLatin1String("windowTitle")));

        // Non-sensitive fields preserved
        QCOMPARE(resultEntry.value(QLatin1String("zoneId")).toString(), QStringLiteral("{abc-123}"));
    }

    void testRedactSessionJson_removesTitle()
    {
        QJsonObject entry;
        entry[QLatin1String("title")] = QStringLiteral("Secret Document.docx");

        QJsonObject root;
        QJsonArray items;
        items.append(entry);
        root[QLatin1String("items")] = items;

        const QString input = QString::fromUtf8(QJsonDocument(root).toJson());
        const QString result = SupportReport::redactSessionJson(input);

        QJsonDocument resultDoc = QJsonDocument::fromJson(result.toUtf8());
        QJsonArray resultItems = resultDoc.object().value(QLatin1String("items")).toArray();
        QVERIFY(!resultItems[0].toObject().contains(QLatin1String("title")));
    }

    void testRedactSessionJson_invalidJson_fallsBackToPathRedaction()
    {
        const QString home = QDir::homePath();
        const QString input = QStringLiteral("not valid json ") + home + QStringLiteral("/test");
        const QString result = SupportReport::redactSessionJson(input);
        QVERIFY(!result.contains(home));
        QVERIFY(result.contains(QStringLiteral("~/test")));
    }

    void testRedactConfigJson_redactsHomePaths()
    {
        const QString home = QDir::homePath();
        const QString input = QStringLiteral("{\"path\": \"") + home + QStringLiteral("/config\"}");
        const QString result = SupportReport::redactConfigJson(input);
        QVERIFY(!result.contains(home));
        QVERIFY(result.contains(QStringLiteral("~/config")));
    }

    void testGenerate_returnsMarkdown()
    {
        // Generate with all nulls — should still produce valid structure
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr, nullptr, 30);
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
        const QString report = SupportReport::generate(nullptr, nullptr, nullptr, nullptr, 999);
        QVERIFY(report.contains(QStringLiteral("last 120 minutes")));
    }
};

QTEST_MAIN(TestSupportReport)

#include "test_support_report.moc"
