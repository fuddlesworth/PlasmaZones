// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_picker_options.cpp
 * @brief The option lists the rule editor's pickers are built from
 *        (RulePickerOptions): the Zone condition's zones and the
 *        running-windows rows.
 */

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
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
        auto* beta = new PhosphorZones::Layout(QStringLiteral("Beta"), &owner);
        PhosphorZones::Zone* betaSecond = addZone(beta, 2, QStringLiteral("Right"));
        PhosphorZones::Zone* betaFirst = addZone(beta, 1, QStringLiteral("  "));
        auto* alpha = new PhosphorZones::Layout(QStringLiteral("alpha"), &owner);
        PhosphorZones::Zone* left = addZone(alpha, 1, QStringLiteral("Left"));

        const QVariantList options = RulePickerOptions::zoneMatchOptions({beta, nullptr, alpha});
        QCOMPARE(options.size(), 3);

        // "alpha" before "Beta": the order ignores case, where code-point
        // order would put the capital first.
        QCOMPARE(optionAt(options, 0).value(QStringLiteral("id")).toString(), left->id().toString());
        QVERIFY(optionAt(options, 0).value(QStringLiteral("id")).toString().startsWith(QLatin1Char('{')));
        QCOMPARE(optionAt(options, 0).value(QStringLiteral("displayName")).toString(), QStringLiteral("alpha — Left"));
        QCOMPARE(optionAt(options, 0).value(QStringLiteral("layoutOrder")).toInt(), 0);

        // Zone 1 before zone 2 whatever the insertion order, and a blank name
        // falls back to the number.
        QCOMPARE(optionAt(options, 1).value(QStringLiteral("id")).toString(), betaFirst->id().toString());
        QCOMPARE(optionAt(options, 1).value(QStringLiteral("zoneName")).toString(), QStringLiteral("Zone 1"));
        QCOMPARE(optionAt(options, 1).value(QStringLiteral("displayName")).toString(), QStringLiteral("Beta — Zone 1"));
        QCOMPARE(optionAt(options, 2).value(QStringLiteral("id")).toString(), betaSecond->id().toString());
        QCOMPARE(optionAt(options, 2).value(QStringLiteral("layoutName")).toString(), QStringLiteral("Beta"));
        QCOMPARE(optionAt(options, 2).value(QStringLiteral("layoutOrder")).toInt(), 1);
    }

    // No two options read alike: layouts sharing a name (ignoring case),
    // a real name already shaped like a suffixed one, names differing only
    // in a slash the picker swaps for U+2215, and zones of one layout sharing
    // a name, or a name equal to an unnamed sibling's "Zone N", all read
    // apart. A name the user typed keeps its plain label whichever item comes
    // first: the real "Grid (2)" and the zone named "Zone 3" are not
    // suffixed, the repeat and the fallback are. An unnamed layout still has
    // a name to group under, and a name holding "%2" is not substituted
    // twice. FAILS if any two labels match or a typed name is suffixed.
    void zoneMatchOptions_noTwoLabelsReadAlike()
    {
        QObject owner;
        auto* first = new PhosphorZones::Layout(QStringLiteral("Grid"), &owner);
        addZone(first, 1, QString());
        auto* second = new PhosphorZones::Layout(QStringLiteral("grid"), &owner);
        addZone(second, 1, QString());
        auto* realSuffix = new PhosphorZones::Layout(QStringLiteral("Grid (2)"), &owner);
        addZone(realSuffix, 1, QString());
        auto* slash = new PhosphorZones::Layout(QStringLiteral("A/B"), &owner);
        addZone(slash, 1, QString());
        auto* lookalike = new PhosphorZones::Layout(QStringLiteral("A∕B"), &owner);
        addZone(lookalike, 1, QString());
        auto* percent = new PhosphorZones::Layout(QStringLiteral("Mine %2"), &owner);
        addZone(percent, 1, QString());
        auto* percentAgain = new PhosphorZones::Layout(QStringLiteral("Mine %2"), &owner);
        addZone(percentAgain, 1, QString());
        auto* repeats = new PhosphorZones::Layout(QStringLiteral("Repeats"), &owner);
        addZone(repeats, 1, QStringLiteral("Main"));
        addZone(repeats, 2, QStringLiteral("Main"));
        PhosphorZones::Zone* unnamedThird = addZone(repeats, 3, QString());
        PhosphorZones::Zone* namedZone3 = addZone(repeats, 4, QStringLiteral("Zone 3"));
        auto* unnamed = new PhosphorZones::Layout(QString(), &owner);
        addZone(unnamed, 1, QStringLiteral("Only"));

        const QVariantList options = RulePickerOptions::zoneMatchOptions(
            {first, second, realSuffix, slash, lookalike, percent, percentAgain, repeats, unnamed});
        QCOMPARE(options.size(), 12);
        QStringList labels;
        QSet<QString> keys;
        QHash<QString, QString> labelById;
        for (const QVariant& o : options) {
            const QVariantMap map = o.toMap();
            const QString label = map.value(QStringLiteral("displayName")).toString();
            labels.append(label);
            keys.insert(label.toLower().replace(QLatin1Char('/'), QChar(0x2215)));
            labelById.insert(map.value(QStringLiteral("id")).toString(), label);
        }
        const QString all = labels.join(QLatin1Char('|'));
        QCOMPARE(keys.size(), labels.size());
        QVERIFY2(labels.contains(QStringLiteral("Grid — Zone 1")), qPrintable(all));
        QVERIFY2(labels.contains(QStringLiteral("grid (3) — Zone 1")), qPrintable(all));
        QVERIFY2(labels.contains(QStringLiteral("Grid (2) — Zone 1")), qPrintable(all));
        QVERIFY2(labels.contains(QStringLiteral("Mine %2 (2) — Zone 1")), qPrintable(all));
        QVERIFY2(labels.contains(QStringLiteral("Repeats — Main (2)")), qPrintable(all));
        QCOMPARE(labelById.value(namedZone3->id().toString()), QStringLiteral("Repeats — Zone 3"));
        QCOMPARE(labelById.value(unnamedThird->id().toString()), QStringLiteral("Repeats — Zone 3 (2)"));
        QVERIFY2(labels.contains(QStringLiteral("Unnamed layout — Only")), qPrintable(all));
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
        QCOMPARE(first.value(QStringLiteral("appName")).toString(), QStringLiteral("konsole"));
        QCOMPARE(first.value(QStringLiteral("caption")).toString(), QStringLiteral("Shell"));
        const QVariantMap second = parsed.at(1).toMap();
        QCOMPARE(second.value(QStringLiteral("rawWindowClass")).toString(), QStringLiteral("steam"));
        QCOMPARE(second.value(QStringLiteral("desktopFile")).toString(), QString());
        const QVariantMap third = parsed.at(2).toMap();
        QCOMPARE(third.value(QStringLiteral("rawWindowClass")).toString(), QString());
        QCOMPARE(third.value(QStringLiteral("caption")).toString(), QStringLiteral("Gamescope"));
    }

    // A payload that is not a JSON array gives an empty list and says why:
    // the caller sees an empty list either way, so the warning is what tells
    // "no windows are open" from "the effect sent something unreadable".
    // FAILS if either guard or its warning is dropped (an unmatched
    // ignoreMessage fails the test).
    void runningWindows_rejectsMalformedPayloads()
    {
        QVERIFY(RulePickerOptions::runningWindows(QString()).isEmpty());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not valid JSON")));
        QVERIFY(RulePickerOptions::runningWindows(QStringLiteral("not json")).isEmpty());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("not an array")));
        QVERIFY(RulePickerOptions::runningWindows(QStringLiteral("{\"windowClass\":\"x\"}")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRulePickerOptions)
#include "test_rule_picker_options.moc"
