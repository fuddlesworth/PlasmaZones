// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_picker_options.cpp
 * @brief The option lists the rule editor's pickers are built from
 *        (RulePickerOptions): the Zone condition's zones and the
 *        running-windows rows.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTest>
#include <QUuid>
#include <QVariantMap>

#include "settings/rules/rulepickeroptions.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;

namespace {

PhosphorZones::Zone* addZone(PhosphorZones::Layout* layout, int number, const QString& name)
{
    auto* zone = new PhosphorZones::Zone(layout);
    zone->setZoneNumber(number);
    zone->setName(name);
    layout->addZone(zone);
    return zone;
}

QVariantMap optionAt(const QVariantList& options, int index)
{
    return options.at(index).toMap();
}

} // namespace

class TestRulePickerOptions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Layouts in the layout pickers' order (name, ignoring case), zones by
    // number, ids braced, labels "<layout> — <zone>" with "Zone N" for an
    // unnamed zone. FAILS if the id loses its braces (the effect's zone cache
    // stamps the braced form, so every pick would silently never match).
    void zoneMatchOptions_orderIdsAndLabels()
    {
        QObject owner;
        auto* wide = new PhosphorZones::Layout(QStringLiteral("wide"), &owner);
        PhosphorZones::Zone* wideSecond = addZone(wide, 2, QStringLiteral("Right"));
        PhosphorZones::Zone* wideFirst = addZone(wide, 1, QStringLiteral("  "));
        auto* columns = new PhosphorZones::Layout(QStringLiteral("Columns"), &owner);
        PhosphorZones::Zone* left = addZone(columns, 1, QStringLiteral("Left"));

        const QVariantList options = RulePickerOptions::zoneMatchOptions({wide, nullptr, columns});
        QCOMPARE(options.size(), 3);

        // "Columns" before "wide": the order ignores case.
        QCOMPARE(optionAt(options, 0).value(QStringLiteral("id")).toString(), left->id().toString());
        QVERIFY(optionAt(options, 0).value(QStringLiteral("id")).toString().startsWith(QLatin1Char('{')));
        QCOMPARE(optionAt(options, 0).value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("Columns — Left"));
        QCOMPARE(optionAt(options, 0).value(QStringLiteral("layoutOrder")).toInt(), 0);

        // Zone 1 before zone 2 whatever the insertion order, and a blank name
        // falls back to the number.
        QCOMPARE(optionAt(options, 1).value(QStringLiteral("id")).toString(), wideFirst->id().toString());
        QCOMPARE(optionAt(options, 1).value(QStringLiteral("zoneName")).toString(), QStringLiteral("Zone 1"));
        QCOMPARE(optionAt(options, 1).value(QStringLiteral("displayName")).toString(), QStringLiteral("wide — Zone 1"));
        QCOMPARE(optionAt(options, 2).value(QStringLiteral("id")).toString(), wideSecond->id().toString());
        QCOMPARE(optionAt(options, 2).value(QStringLiteral("layoutName")).toString(), QStringLiteral("wide"));
        QCOMPARE(optionAt(options, 2).value(QStringLiteral("layoutOrder")).toInt(), 1);
    }

    // Two layouts sharing a name read apart, and an unnamed layout still has
    // a name to group under. FAILS if same-named layouts' zones share labels.
    void zoneMatchOptions_disambiguatesLayoutNames()
    {
        QObject owner;
        auto* first = new PhosphorZones::Layout(QStringLiteral("Grid"), &owner);
        addZone(first, 1, QString());
        auto* second = new PhosphorZones::Layout(QStringLiteral("grid"), &owner);
        addZone(second, 1, QString());
        auto* unnamed = new PhosphorZones::Layout(QString(), &owner);
        addZone(unnamed, 1, QStringLiteral("Only"));

        const QVariantList options = RulePickerOptions::zoneMatchOptions({first, second, unnamed});
        QCOMPARE(options.size(), 3);
        QStringList labels;
        for (const QVariant& o : options) {
            labels.append(o.toMap().value(QStringLiteral("displayName")).toString());
        }
        QVERIFY2(labels.contains(QStringLiteral("Grid — Zone 1")), qPrintable(labels.join(QLatin1Char('|'))));
        QVERIFY2(labels.contains(QStringLiteral("grid (2) — Zone 1")), qPrintable(labels.join(QLatin1Char('|'))));
        QVERIFY2(labels.contains(QStringLiteral("Unnamed layout — Only")), qPrintable(labels.join(QLatin1Char('|'))));
    }

    void zoneMatchOptions_emptyInput()
    {
        QVERIFY(RulePickerOptions::zoneMatchOptions({}).isEmpty());
        QVERIFY(RulePickerOptions::zoneMatchOptions({nullptr}).isEmpty());
    }

    // Every field is carried through, and a row from an effect older than
    // rawWindowClass falls back to its windowClass so Classes mode keeps
    // offering something. FAILS if the fallback is lost.
    void runningWindows_parsesRowsAndFallsBack()
    {
        QJsonObject current;
        current[QStringLiteral("windowClass")] = QStringLiteral("org.kde.konsole");
        current[QStringLiteral("rawWindowClass")] = QStringLiteral("konsole org.kde.konsole");
        current[QStringLiteral("appName")] = QStringLiteral("konsole");
        current[QStringLiteral("caption")] = QStringLiteral("Shell");
        current[QStringLiteral("desktopFile")] = QStringLiteral("org.kde.konsole");
        QJsonObject old;
        old[QStringLiteral("windowClass")] = QStringLiteral("steam");
        old[QStringLiteral("appName")] = QStringLiteral("steam");
        old[QStringLiteral("caption")] = QStringLiteral("Steam");
        QJsonObject classless;
        classless[QStringLiteral("windowClass")] = QString();
        classless[QStringLiteral("rawWindowClass")] = QString();
        classless[QStringLiteral("appName")] = QString();
        classless[QStringLiteral("caption")] = QStringLiteral("Gamescope");
        classless[QStringLiteral("desktopFile")] = QString();
        const QJsonArray rows{current, QJsonValue(42), old, classless};
        const QVariantList parsed =
            RulePickerOptions::runningWindows(QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact)));

        QCOMPARE(parsed.size(), 3); // the non-object row is skipped
        const QVariantMap first = parsed.at(0).toMap();
        QCOMPARE(first.value(QStringLiteral("rawWindowClass")).toString(), QStringLiteral("konsole org.kde.konsole"));
        QCOMPARE(first.value(QStringLiteral("windowClass")).toString(), QStringLiteral("org.kde.konsole"));
        QCOMPARE(first.value(QStringLiteral("desktopFile")).toString(), QStringLiteral("org.kde.konsole"));
        const QVariantMap second = parsed.at(1).toMap();
        QCOMPARE(second.value(QStringLiteral("rawWindowClass")).toString(), QStringLiteral("steam"));
        QCOMPARE(second.value(QStringLiteral("desktopFile")).toString(), QString());
        const QVariantMap third = parsed.at(2).toMap();
        QCOMPARE(third.value(QStringLiteral("rawWindowClass")).toString(), QString());
        QCOMPARE(third.value(QStringLiteral("caption")).toString(), QStringLiteral("Gamescope"));
    }

    void runningWindows_rejectsMalformedPayloads()
    {
        QVERIFY(RulePickerOptions::runningWindows(QString()).isEmpty());
        QVERIFY(RulePickerOptions::runningWindows(QStringLiteral("not json")).isEmpty());
        QVERIFY(RulePickerOptions::runningWindows(QStringLiteral("{\"windowClass\":\"x\"}")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRulePickerOptions)
#include "test_rule_picker_options.moc"
