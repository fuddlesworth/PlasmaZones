// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins ShortcutManager::compressCheatsheetFamilies: a fully-assigned,
// single-trigger, prefix-sharing family collapses to one combined row, and
// every deviation (alternate binding, prefix mismatch, unassigned member,
// off-pattern final token) falls back to the individual rows.

#include "daemon/controllers/shortcutmanager.h"

#include <QtTest/QtTest>

using PlasmaZones::ShortcutManager;

namespace {

QVariantMap makeRow(const QString& id, const QStringList& triggers)
{
    QVariantMap r;
    r.insert(QStringLiteral("id"), id);
    r.insert(QStringLiteral("label"), id);
    r.insert(QStringLiteral("category"), QStringLiteral("Navigation"));
    r.insert(QStringLiteral("categoryOrder"), 1);
    r.insert(QStringLiteral("triggers"), triggers);
    r.insert(QStringLiteral("assigned"), !triggers.isEmpty());
    r.insert(QStringLiteral("mode"), QStringLiteral("all"));
    return r;
}

ShortcutManager::CheatsheetFamily arrowFamily()
{
    ShortcutManager::CheatsheetFamily f;
    f.ids = {QStringLiteral("span-left"), QStringLiteral("span-right"), QStringLiteral("span-up"),
             QStringLiteral("span-down")};
    f.expectedLastTokens = {QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Up"),
                            QStringLiteral("Down")};
    f.combinedLabel = QStringLiteral("Span Window");
    f.tailToken = QStringLiteral("Arrows");
    return f;
}

QVector<QVariantMap> arrowRows()
{
    return {makeRow(QStringLiteral("span-left"), {QStringLiteral("Ctrl+Alt+Left")}),
            makeRow(QStringLiteral("span-right"), {QStringLiteral("Ctrl+Alt+Right")}),
            makeRow(QStringLiteral("span-up"), {QStringLiteral("Ctrl+Alt+Up")}),
            makeRow(QStringLiteral("span-down"), {QStringLiteral("Ctrl+Alt+Down")})};
}

} // namespace

class TestCheatsheetCompression : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fullFamily_compressesToOneRow()
    {
        const QVector<QVariantMap> out = ShortcutManager::compressCheatsheetFamilies(arrowRows(), {arrowFamily()});

        QCOMPARE(out.size(), 1);
        const QVariantMap row = out.first();
        QCOMPARE(row.value(QStringLiteral("label")).toString(), QStringLiteral("Span Window"));
        QCOMPARE(row.value(QStringLiteral("triggers")).toStringList(),
                 (QStringList{QStringLiteral("Ctrl+Alt+Arrows")}));
    }

    void alternateBinding_staysUncompressed()
    {
        // A member carrying a second effective trigger must keep the family
        // uncompressed so the extra binding stays visible on the sheet.
        QVector<QVariantMap> rows = arrowRows();
        rows[1].insert(QStringLiteral("triggers"),
                       QStringList{QStringLiteral("Ctrl+Alt+Right"), QStringLiteral("Meta+F1")});

        const QVector<QVariantMap> out = ShortcutManager::compressCheatsheetFamilies(rows, {arrowFamily()});

        QCOMPARE(out.size(), 4);
        QCOMPARE(out.at(1).value(QStringLiteral("triggers")).toStringList(),
                 (QStringList{QStringLiteral("Ctrl+Alt+Right"), QStringLiteral("Meta+F1")}));
    }

    void prefixMismatch_staysUncompressed()
    {
        QVector<QVariantMap> rows = arrowRows();
        rows[2].insert(QStringLiteral("triggers"), QStringList{QStringLiteral("Meta+Up")});

        QCOMPARE(ShortcutManager::compressCheatsheetFamilies(rows, {arrowFamily()}).size(), 4);
    }

    void unassignedMember_staysUncompressed()
    {
        // Pins the unassigned OUTCOME: an empty trigger list fails the
        // single-trigger requirement (the producer derives "assigned" from
        // triggers-non-empty, so there is no separate assigned guard).
        QVector<QVariantMap> rows = arrowRows();
        rows[3].insert(QStringLiteral("triggers"), QStringList());
        rows[3].insert(QStringLiteral("assigned"), false);

        QCOMPARE(ShortcutManager::compressCheatsheetFamilies(rows, {arrowFamily()}).size(), 4);
    }

    void wrongFinalToken_staysUncompressed()
    {
        QVector<QVariantMap> rows = arrowRows();
        rows[0].insert(QStringLiteral("triggers"), QStringList{QStringLiteral("Ctrl+Alt+Home")});

        QCOMPARE(ShortcutManager::compressCheatsheetFamilies(rows, {arrowFamily()}).size(), 4);
    }

    void bareTokenBinding_staysUncompressed()
    {
        // A member bound to a modifier-less key has no '+' to split on;
        // compressing it would emit a bogus "+Arrows" chip with an empty
        // shared prefix.
        QVector<QVariantMap> rows = arrowRows();
        rows[0].insert(QStringLiteral("triggers"), QStringList{QStringLiteral("Left")});

        QCOMPARE(ShortcutManager::compressCheatsheetFamilies(rows, {arrowFamily()}).size(), 4);
    }

    void missingFamilyMember_leavesRowsAlone()
    {
        // A family id absent from the rows (e.g. entries not yet registered)
        // must not compress or drop anything.
        QVector<QVariantMap> rows = arrowRows();
        rows.removeLast();

        QCOMPARE(ShortcutManager::compressCheatsheetFamilies(rows, {arrowFamily()}).size(), 3);
    }
};

QTEST_GUILESS_MAIN(TestCheatsheetCompression)
#include "test_cheatsheet_compression.moc"
