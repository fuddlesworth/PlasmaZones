// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "../src/shortcutreferencemodel.h"

#include <QCoreApplication>
#include <QEvent>
#include <QSignalSpy>
#include <QTest>
#include <QTranslator>

#include <utility>

using PhosphorShellDashboard::ShortcutReferenceModel;

namespace {
QVariantMap action(const QString& id, const QString& mode, const QStringList& triggers = {}, const QString& label = {})
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("mode"), mode},
            {QStringLiteral("label"), label.isEmpty() ? id : label},
            {QStringLiteral("triggers"), triggers}};
}

QVariantList directionalFamily()
{
    QVariantList rows;
    for (const auto& direction :
         {QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Up"), QStringLiteral("Down")})
        rows.append(action(QStringLiteral("focus_zone_") + direction.toLower(), QStringLiteral("all"),
                           {QStringLiteral("Alt+Shift+") + direction}, QStringLiteral("Focus ") + direction));
    return rows;
}

QVariantList displayed(const ShortcutReferenceModel& model)
{
    QVariantList rows;
    for (const auto& group : model.groups())
        rows.append(group.toMap().value(QLatin1String("rows")).toList());
    return rows;
}

QStringList ids(const ShortcutReferenceModel& model)
{
    QStringList result;
    for (const auto& row : displayed(model))
        result.append(row.toMap().value(QLatin1String("id")).toString());
    return result;
}

QVariantList groupRows(const ShortcutReferenceModel& model, const QString& id)
{
    for (const auto& value : model.groups()) {
        const auto group = value.toMap();
        if (group.value(QLatin1String("id")).toString() == id)
            return group.value(QLatin1String("rows")).toList();
    }
    return {};
}

QStringList rowIds(const QVariantList& rows)
{
    QStringList result;
    for (const auto& row : rows)
        result.append(row.toMap().value(QLatin1String("id")).toString());
    return result;
}

class ReferenceTranslator : public QTranslator
{
public:
    bool isEmpty() const override
    {
        return false;
    }
    QString translate(const char* context, const char* source, const char*, int) const override
    {
        if (qstrcmp(context, "PhosphorShellDashboard") != 0)
            return {};
        if (qstrcmp(source, "Focus") == 0)
            return QStringLiteral("Fokus");
        if (qstrcmp(source, "Enter") == 0)
            return QStringLiteral("Eingabe");
        return {};
    }
};
} // namespace

class TestShortcutReferenceModel : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void modesAndCapability()
    {
        ShortcutReferenceModel model;
        auto rows = directionalFamily();
        rows.append(
            action(QStringLiteral("focus_master"), QStringLiteral("autotile"), {QStringLiteral("Meta+Shift+M")}));
        rows.append(action(QStringLiteral("retile"), QStringLiteral("managed"), {QStringLiteral("Meta+Ctrl+T")}));
        rows.append(action(QStringLiteral("span_window_left"), QStringLiteral("snapping")));
        rows.append(action(QStringLiteral("scroll_center_column"), QStringLiteral("scrolling")));
        rows.append(action(QStringLiteral("layout_picker"), QStringLiteral("layouts")));
        rows.append(action(QStringLiteral("open_settings"), QStringLiteral("all")));
        rows.append(action(QStringLiteral("swap_virtual_screen_left"), QStringLiteral("all")));
        model.setRows(rows);
        QCOMPARE(model.count(), 7); // shared focus, master, retile, layout
        QVERIFY(ids(model).contains(QStringLiteral("focus_master")));
        QVERIFY(!ids(model).contains(QStringLiteral("open_settings")));
        model.setScope(QStringLiteral("scrolling"));
        QCOMPARE(model.count(), 7);
        QVERIFY(!ids(model).contains(QStringLiteral("focus_master")));
        QVERIFY(ids(model).contains(QStringLiteral("scroll_center_column")));
        model.setScope(QStringLiteral("snapping"));
        QCOMPARE(model.count(), 6);
        QVERIFY(!ids(model).contains(QStringLiteral("retile")));
        model.setLayoutsAvailable(false);
        QCOMPARE(model.count(), 5);
        model.setScope(QStringLiteral("general"));
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.groups().size(), 2);
        QCOMPARE(model.groups()[0].toMap().value(QLatin1String("id")).toString(), QStringLiteral("general"));
        QCOMPARE(model.groups()[1].toMap().value(QLatin1String("id")).toString(), QStringLiteral("virtual-screens"));
        model.setScope(QStringLiteral("all"));
        QCOMPARE(model.count(), 18); // ten applicable native actions + eight external
        QVERIFY(!ids(model).contains(QStringLiteral("layout_picker")));
    }

    void shellCommandsAreExternalAndNeverInventBindings()
    {
        ShortcutReferenceModel model;
        model.setRows(
            {action(QStringLiteral("launcher.toggle"), QStringLiteral("all"), {QStringLiteral("Meta+Space")})});
        model.setScope(QStringLiteral("shell"));
        QCOMPARE(model.count(), 8);
        for (const auto& value : displayed(model)) {
            const auto row = value.toMap();
            QVERIFY(row.value(QLatin1String("external")).toBool());
            QVERIFY(!row.value(QLatin1String("assigned")).toBool());
            QVERIFY(row.value(QLatin1String("triggers")).toStringList().isEmpty());
        }
        model.setAssignedOnly(true);
        QCOMPARE(model.count(), 0);
        model.setAssignedOnly(false);
        model.setQuery(QStringLiteral("quick settings"));
        QCOMPARE(ids(model), QStringList{QStringLiteral("control-center.toggle")});
    }

    void completeFamiliesPreserveChildrenAndCount()
    {
        ShortcutReferenceModel model;
        model.setRows(directionalFamily());
        QCOMPARE(model.count(), 4);
        QCOMPARE(displayed(model).size(), 1);
        auto family = displayed(model).first().toMap();
        QVERIFY(family.value(QLatin1String("family")).toBool());
        QCOMPARE(family.value(QLatin1String("id")).toString(), QStringLiteral("family:focus_zone"));
        QCOMPARE(family.value(QLatin1String("children")).toList().size(), 4);
        QVERIFY(family.value(QLatin1String("triggers")).toStringList().isEmpty());
        QCOMPARE(family.value(QLatin1String("keyParts")).toStringList().last(), QStringLiteral("← → ↑ ↓"));
        model.setQuery(QStringLiteral("focus"));
        QCOMPARE(displayed(model).size(), 4); // Search exposes each matching action.
        model.setQuery(QString());
        auto rows = directionalFamily();
        auto left = rows[0].toMap();
        left.insert(QStringLiteral("triggers"),
                    QStringList{QStringLiteral("Meta+H"), QStringLiteral("Alt+Shift+Left")});
        rows[0] = left;
        model.setRows(rows);
        family = displayed(model).first().toMap();
        QVERIFY(family.value(QLatin1String("keyParts")).toStringList().isEmpty());
        QCOMPARE(family.value(QLatin1String("children"))
                     .toList()[0]
                     .toMap()
                     .value(QLatin1String("triggers"))
                     .toStringList()
                     .size(),
                 2);
    }

    void incompleteOffPatternAndUnassignedFamiliesDoNotCompress()
    {
        ShortcutReferenceModel model;
        auto rows = directionalFamily();
        rows.removeLast();
        model.setRows(rows);
        QVERIFY(displayed(model).first().toMap().value(QLatin1String("keyParts")).toStringList().isEmpty());
        rows = directionalFamily();
        for (qsizetype i = 0; i < rows.size(); ++i) {
            auto row = rows[i].toMap();
            row.insert(QStringLiteral("triggers"),
                       QStringList{QStringLiteral("Alt+Shift+") + QStringLiteral("ABCD").mid(i, 1)});
            rows[i] = row;
        }
        model.setRows(rows);
        QVERIFY(displayed(model).first().toMap().value(QLatin1String("keyParts")).toStringList().isEmpty());
        rows[0] = action(QStringLiteral("focus_zone_left"), QStringLiteral("all"));
        model.setRows(rows);
        model.setAssignedOnly(true);
        QCOMPARE(model.count(), 3);
        QCOMPARE(displayed(model).first().toMap().value(QLatin1String("children")).toList().size(), 3);
        QVERIFY(displayed(model).first().toMap().value(QLatin1String("keyParts")).toStringList().isEmpty());
    }

    void numberedFamiliesAndUnassignedSearch()
    {
        ShortcutReferenceModel model;
        QVariantList rows;
        for (int i = 1; i <= 9; ++i)
            rows.append(action(QStringLiteral("snap_to_zone_") + QString::number(i), QStringLiteral("all"),
                               {QStringLiteral("Meta+Ctrl+") + QString::number(i)}));
        model.setRows(rows);
        QCOMPARE(model.count(), 9);
        QCOMPARE(displayed(model).first().toMap().value(QLatin1String("keyParts")).toStringList().last(),
                 QStringLiteral("1–9"));
        rows[3] = action(QStringLiteral("snap_to_zone_4"), QStringLiteral("all"));
        model.setRows(rows);
        QVERIFY(displayed(model).first().toMap().value(QLatin1String("keyParts")).toStringList().isEmpty());
        model.setQuery(QStringLiteral("unassigned"));
        QCOMPARE(ids(model), QStringList{QStringLiteral("snap_to_zone_4")});
        model.setQuery(QStringLiteral("Control Super 3"));
        QCOMPARE(ids(model), QStringList{QStringLiteral("snap_to_zone_3")});
    }

    void alternateSearchDoesNotCombineDifferentBindings()
    {
        ShortcutReferenceModel model;
        model.setRows(
            {action(QStringLiteral("focus_zone_left"), QStringLiteral("all"),
                    {QStringLiteral("Meta+H"), QStringLiteral("Alt+Shift+Left")}, QStringLiteral("Move focus left"))});
        for (const auto& query :
             {QStringLiteral("Super H"), QStringLiteral("Windows key H"), QStringLiteral("Alt Shift Left"),
              QStringLiteral("focus super h"), QStringLiteral("alt shift ←")}) {
            model.setQuery(query);
            QCOMPARE(model.count(), 1);
        }
        for (const auto& query :
             {QStringLiteral("Meta Shift H"), QStringLiteral("Meta Alt"), QStringLiteral("Super Left")}) {
            model.setQuery(query);
            QCOMPARE(model.count(), 0);
        }
        model.setQuery(QString());
        const auto bindings = displayed(model).first().toMap().value(QLatin1String("bindings")).toList();
        QCOMPARE(bindings.size(), 2);
        QCOMPARE(bindings[1].toStringList().last(), QStringLiteral("←"));
    }

    void functionAndNamedKeysStayWithinOneAlternative()
    {
        ShortcutReferenceModel model;
        model.setRows({action(QStringLiteral("focus_zone_left"), QStringLiteral("all"),
                              {QStringLiteral("Meta+H"), QStringLiteral("Ctrl+F1"), QStringLiteral("Alt+Del"),
                               QStringLiteral("Ctrl+VolumeDown")})});
        for (const auto& query : {QStringLiteral("Meta F1"), QStringLiteral("Meta Delete"), QStringLiteral("Alt F1"),
                                  QStringLiteral("Meta VolumeDown")}) {
            model.setQuery(query);
            QCOMPARE(model.count(), 0);
        }
        for (const auto& query : {QStringLiteral("Control F1"), QStringLiteral("F1"), QStringLiteral("Alt Delete"),
                                  QStringLiteral("Ctrl VolumeDown")}) {
            model.setQuery(query);
            QCOMPARE(model.count(), 1);
        }
    }

    void whitespaceBindingsRemainUnassigned()
    {
        ShortcutReferenceModel model;
        model.setRows({action(QStringLiteral("focus_zone_left"), QStringLiteral("all"),
                              {QString(), QStringLiteral("   "), QStringLiteral("\t")})});
        QCOMPARE(model.count(), 1);
        const auto row = displayed(model).first().toMap();
        QVERIFY(!row.value(QLatin1String("assigned")).toBool());
        QVERIFY(row.value(QLatin1String("bindings")).toList().isEmpty());
        model.setAssignedOnly(true);
        QCOMPARE(model.count(), 0);
    }

    void reentrantQueryPublishesACoherentResult()
    {
        ShortcutReferenceModel model;
        connect(&model, &ShortcutReferenceModel::groupsChanged, &model, [&model] {
            if (model.query().isEmpty())
                model.setQuery(QStringLiteral("no such shortcut"));
        });
        model.setRows(directionalFamily());
        QVERIFY(model.groups().isEmpty());
        QCOMPARE(model.count(), 0);
    }

    void keyAliasesLiteralPlusAndDescriptions()
    {
        ShortcutReferenceModel model;
        auto plus =
            action(QStringLiteral("increase_master_ratio"), QStringLiteral("autotile"), {QStringLiteral("Meta++")});
        plus.insert(QStringLiteral("description"), QStringLiteral("Grow the master area"));
        model.setRows(
            {plus,
             action(QStringLiteral("swap_master"), QStringLiteral("autotile"), {QStringLiteral("Meta+Shift+Return")}),
             action(QStringLiteral("restore_window_size"), QStringLiteral("all"), {QStringLiteral("Meta+Escape")}),
             action(QStringLiteral("scroll_view_page_back"), QStringLiteral("scrolling"),
                    {QStringLiteral("Meta+PgDown")})});
        model.setQuery(QStringLiteral("Meta++"));
        QCOMPARE(ids(model), QStringList{QStringLiteral("increase_master_ratio")});
        model.setQuery(QStringLiteral("meta plus"));
        QCOMPARE(model.count(), 1);
        model.setQuery(QStringLiteral("grow master"));
        QCOMPARE(model.count(), 1);
        model.setQuery(QStringLiteral("enter"));
        QCOMPARE(ids(model), QStringList{QStringLiteral("swap_master")});
        model.setQuery(QStringLiteral("esc"));
        QCOMPARE(ids(model), QStringList{QStringLiteral("restore_window_size")});
        model.setScope(QStringLiteral("scrolling"));
        model.setQuery(QStringLiteral("page down"));
        QCOMPARE(ids(model), QStringList{QStringLiteral("scroll_view_page_back")});
        QCOMPARE(model.keyPartsForTrigger(QStringLiteral("Meta++")),
                 (QStringList{QStringLiteral("Meta"), QStringLiteral("+")}));
    }

    void scrollingUsesTemplatesExplanation()
    {
        ShortcutReferenceModel model;
        auto row = action(QStringLiteral("layout_picker"), QStringLiteral("layouts"));
        row.insert(QStringLiteral("description"), QStringLiteral("Pick a layout"));
        row.insert(QStringLiteral("templatesDescription"), QStringLiteral("Pick a column template"));
        model.setRows({row});
        QCOMPARE(displayed(model).first().toMap().value(QLatin1String("description")).toString(),
                 QStringLiteral("Pick a layout"));
        model.setScope(QStringLiteral("scrolling"));
        QCOMPARE(displayed(model).first().toMap().value(QLatin1String("description")).toString(),
                 QStringLiteral("Pick a column template"));
    }

    void primaryControlsFollowReferenceOrderWithoutChangingBindings()
    {
        ShortcutReferenceModel model;
        model.setScope(QStringLiteral("all"));
        // The daemon registers shared actions ahead of its direction/master
        // controls. Reversing family members also exercises child ordering.
        QVariantList rows{
            action(QStringLiteral("toggle_window_float"), QStringLiteral("all")),
            action(QStringLiteral("scroll_switch_focus_float_tiling"), QStringLiteral("all")),
            action(QStringLiteral("cycle_window_backward"), QStringLiteral("all")),
            action(QStringLiteral("scroll_focus_column_last"), QStringLiteral("scrolling")),
            action(QStringLiteral("focus_master"), QStringLiteral("autotile"),
                   {QStringLiteral("Meta+Z"), QStringLiteral("Alt+M")}, QStringLiteral("Translated master label")),
            action(QStringLiteral("swap_master"), QStringLiteral("autotile")),
            action(QStringLiteral("cycle_window_forward"), QStringLiteral("all")),
            action(QStringLiteral("scroll_focus_column_first"), QStringLiteral("scrolling")),
            action(QStringLiteral("rotate_windows_counterclockwise"), QStringLiteral("all")),
            action(QStringLiteral("rotate_windows_clockwise"), QStringLiteral("all")),
        };
        for (const auto* prefix : {"swap_window_", "move_window_", "focus_zone_"}) {
            for (const auto* direction : {"down", "up", "right", "left"}) {
                const QString id = QString::fromLatin1(prefix) + QString::fromLatin1(direction);
                rows.append(action(id, QStringLiteral("all")));
            }
        }
        for (int i = 9; i >= 1; --i)
            rows.append(action(QStringLiteral("snap_to_zone_") + QString::number(i), QStringLiteral("all")));
        model.setRows(rows);
        const auto focus = groupRows(model, QStringLiteral("focus"));
        QCOMPARE(
            rowIds(focus),
            (QStringList{QStringLiteral("family:focus_zone"), QStringLiteral("focus_master"),
                         QStringLiteral("scroll_switch_focus_float_tiling"), QStringLiteral("cycle_window_forward"),
                         QStringLiteral("cycle_window_backward"), QStringLiteral("scroll_focus_column_first"),
                         QStringLiteral("scroll_focus_column_last")}));
        const auto master = focus[1].toMap();
        QCOMPARE(master.value(QLatin1String("label")).toString(), QStringLiteral("Translated master label"));
        QCOMPARE(master.value(QLatin1String("triggers")).toStringList(),
                 (QStringList{QStringLiteral("Meta+Z"), QStringLiteral("Alt+M")}));
        QCOMPARE(master.value(QLatin1String("mode")).toString(), QStringLiteral("autotile"));
        QCOMPARE(rowIds(focus.first().toMap().value(QLatin1String("children")).toList()),
                 (QStringList{QStringLiteral("focus_zone_left"), QStringLiteral("focus_zone_right"),
                              QStringLiteral("focus_zone_up"), QStringLiteral("focus_zone_down")}));
        QCOMPARE(rowIds(groupRows(model, QStringLiteral("arrange"))),
                 (QStringList{QStringLiteral("family:move_window"), QStringLiteral("family:swap_window"),
                              QStringLiteral("family:snap_to_zone"), QStringLiteral("swap_master"),
                              QStringLiteral("toggle_window_float"), QStringLiteral("rotate_windows_clockwise"),
                              QStringLiteral("rotate_windows_counterclockwise")}));
    }

    void referenceSizingAndUnknownExtensionsKeepStableOrder()
    {
        ShortcutReferenceModel model;
        model.setScope(QStringLiteral("all"));
        QVariantList rows{
            action(QStringLiteral("retile"), QStringLiteral("managed")),
            action(QStringLiteral("decrease_master_count"), QStringLiteral("autotile")),
            action(QStringLiteral("restore_window_size"), QStringLiteral("all")),
            action(QStringLiteral("increase_master_ratio"), QStringLiteral("autotile")),
            action(QStringLiteral("scroll_equalize_window_heights"), QStringLiteral("scrolling")),
            action(QStringLiteral("scroll_increase_window_height"), QStringLiteral("scrolling")),
            action(QStringLiteral("scroll_cycle_window_height"), QStringLiteral("scrolling")),
        };
        constexpr std::pair<const char*, int> extensions[] = {{"focus_extension_late", 50},
                                                              {"focus_extension_first", 10},
                                                              {"focus_extension_second", 10}};
        for (const auto& [id, order] : extensions) {
            auto row = action(QString::fromLatin1(id), QStringLiteral("all"));
            row.insert(QStringLiteral("rowOrder"), order);
            rows.append(row);
        }
        rows.append(action(QStringLiteral("focus_master"), QStringLiteral("autotile")));
        rows.append(action(QStringLiteral("focus_new_a"), QStringLiteral("all")));
        rows.append(action(QStringLiteral("focus_new_b"), QStringLiteral("all")));
        model.setRows(rows);
        QCOMPARE(rowIds(groupRows(model, QStringLiteral("sizing"))),
                 (QStringList{QStringLiteral("increase_master_ratio"), QStringLiteral("decrease_master_count"),
                              QStringLiteral("restore_window_size"), QStringLiteral("retile")}));
        QCOMPARE(
            rowIds(groupRows(model, QStringLiteral("window-size"))),
            (QStringList{QStringLiteral("scroll_cycle_window_height"), QStringLiteral("scroll_increase_window_height"),
                         QStringLiteral("scroll_equalize_window_heights")}));
        QCOMPARE(rowIds(groupRows(model, QStringLiteral("focus"))),
                 (QStringList{QStringLiteral("focus_master"), QStringLiteral("focus_extension_first"),
                              QStringLiteral("focus_extension_second"), QStringLiteral("focus_extension_late"),
                              QStringLiteral("focus_new_a"), QStringLiteral("focus_new_b")}));
    }

    void notificationsDuplicatesAndTranslation()
    {
        ShortcutReferenceModel model;
        QSignalSpy groups(&model, &ShortcutReferenceModel::groupsChanged);
        QSignalSpy count(&model, &ShortcutReferenceModel::countChanged);
        auto rows = directionalFamily();
        rows.append(rows.first()); // A duplicate ID never gains another action.
        rows.append(QVariantMap{});
        model.setRows(rows);
        QCOMPARE(model.count(), 4);
        QCOMPARE(count.count(), 1);
        QCOMPARE(groups.count(), 1);
        model.setRows(rows);
        model.setScope(QStringLiteral("tiling"));
        model.setScope(QStringLiteral("invalid"));
        model.setAssignedOnly(false);
        model.setLayoutsAvailable(true);
        QCOMPARE(groups.count(), 1);
        ReferenceTranslator translator;
        QCoreApplication::installTranslator(&translator);
        QEvent change(QEvent::LanguageChange);
        QCoreApplication::sendEvent(&model, &change);
        QCOMPARE(model.groups().first().toMap().value(QLatin1String("label")).toString(), QStringLiteral("Fokus"));
        QCOMPARE(model.keyPartsForTrigger(QStringLiteral("Return")), QStringList{QStringLiteral("Eingabe")});
        QCoreApplication::removeTranslator(&translator);
    }
};
QTEST_GUILESS_MAIN(TestShortcutReferenceModel)
#include "test_shortcutreferencemodel.moc"
