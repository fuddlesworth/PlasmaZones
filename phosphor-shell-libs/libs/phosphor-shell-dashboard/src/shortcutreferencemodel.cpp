// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "shortcutreferencemodel.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHash>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <limits>

namespace PhosphorShellDashboard {
namespace {
struct GroupSpec
{
    const char* id;
    const char* label;
    const char* icon;
};
constexpr GroupSpec Groups[] = {
    {"focus", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Focus"), "eye"},
    {"arrange", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Arrange"), "grid"},
    {"zones", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Zones"), "grid"},
    {"columns", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Columns"), "grid"},
    {"sizing", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Sizing"), "tune"},
    {"view", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "View"), "eye"},
    {"tabs", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Tabs"), "folder"},
    {"column-size", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Column size"), "tune"},
    {"window-size", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Window size"), "tune"},
    {"layouts", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Layouts"), "grid"},
    {"general", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "General"), "keyboard"},
    {"virtual-screens", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Virtual screens"), "grid"},
    {"shell", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Shell"), "keyboard"},
};
enum Group : int {
    Focus,
    Arrange,
    Zones,
    Columns,
    Sizing,
    View,
    Tabs,
    ColumnSize,
    WindowSize,
    Layouts,
    General,
    VirtualScreens,
    Shell,
};

struct FamilySpec
{
    const char* prefix;
    const char* label;
    bool digits;
};
constexpr FamilySpec Families[] = {
    {"focus_zone_", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Focus a window"), false},
    {"move_window_", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Move a window"), false},
    {"swap_window_", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Swap two windows"), false},
    {"span_window_", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Extend or shrink a span"), false},
    {"swap_virtual_screen_", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Swap virtual screens"), false},
    {"snap_to_zone_", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Move to numbered slot"), true},
    {"quick_layout_", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Quick layout / template"), true},
    {"scroll_focus_tab_", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Focus numbered tab"), true},
};

QString translated(const char* source)
{
    return QCoreApplication::translate("PhosphorShellDashboard", source);
}

QStringList parts(const QString& trigger)
{
    // The final plus is a key, not a separator (Meta++).
    if (trigger.endsWith(QLatin1String("++"))) {
        auto result = trigger.chopped(2).split(QLatin1Char('+'), Qt::SkipEmptyParts);
        result.append(QStringLiteral("+"));
        return result;
    }
    if (trigger == QLatin1String("+"))
        return {trigger};
    auto result = trigger.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (auto& part : result)
        part = part.trimmed();
    return result;
}

QString normalized(QString text)
{
    text = text.toCaseFolded();
    static const std::array<std::pair<QRegularExpression, QString>, 11> aliases = {{
        {QRegularExpression(QStringLiteral("\\b(windows key|windows|super|win)\\b")), QStringLiteral("meta")},
        {QRegularExpression(QStringLiteral("\\bcontrol\\b")), QStringLiteral("ctrl")},
        {QRegularExpression(QStringLiteral("\\boption\\b")), QStringLiteral("alt")},
        {QRegularExpression(QStringLiteral("\\breturn\\b")), QStringLiteral("enter")},
        {QRegularExpression(QStringLiteral("\\bescape\\b")), QStringLiteral("esc")},
        {QRegularExpression(QStringLiteral("\\b(page up|pageup)\\b")), QStringLiteral("pgup")},
        {QRegularExpression(QStringLiteral("\\b(page down|pagedown|pgdn)\\b")), QStringLiteral("pgdown")},
        {QRegularExpression(QStringLiteral("\\bautotile\\b")), QStringLiteral("tiling")},
        {QRegularExpression(QStringLiteral("\\bplus\\b")), QStringLiteral("+")},
        {QRegularExpression(QStringLiteral("\\bdel\\b")), QStringLiteral("delete")},
        {QRegularExpression(QStringLiteral("\\bins\\b")), QStringLiteral("insert")},
    }};
    for (const auto& [pattern, replacement] : aliases)
        text.replace(pattern, replacement);
    text.replace(QChar(0x2190), QStringLiteral("left"));
    text.replace(QChar(0x2192), QStringLiteral("right"));
    text.replace(QChar(0x2191), QStringLiteral("up"));
    text.replace(QChar(0x2193), QStringLiteral("down"));
    return text;
}

bool matches(const QVariantMap& row, const QString& query, const GroupSpec& group)
{
    const QString normalizedQuery = normalized(query.trimmed());
    if (normalizedQuery.isEmpty())
        return true;
    auto tokens = normalizedQuery.split(QRegularExpression(QStringLiteral("[\\s+]+")), Qt::SkipEmptyParts);
    if (normalizedQuery.endsWith(QLatin1String("++")) || normalizedQuery == QLatin1String("+")
        || normalizedQuery.endsWith(QLatin1String(" +")))
        tokens.append(QStringLiteral("+"));
    const QStringList modifiers{QStringLiteral("meta"), QStringLiteral("ctrl"), QStringLiteral("alt"),
                                QStringLiteral("shift"), QStringLiteral("num")};
    const QStringList namedKeys{QStringLiteral("enter"),     QStringLiteral("esc"),      QStringLiteral("pgup"),
                                QStringLiteral("pgdown"),    QStringLiteral("home"),     QStringLiteral("end"),
                                QStringLiteral("tab"),       QStringLiteral("space"),    QStringLiteral("+"),
                                QStringLiteral("backspace"), QStringLiteral("delete"),   QStringLiteral("insert"),
                                QStringLiteral("backtab"),   QStringLiteral("capslock"), QStringLiteral("numlock"),
                                QStringLiteral("pause"),     QStringLiteral("print")};
    static const QRegularExpression functionKey(QStringLiteral("^f(?:[1-9]|[12][0-9]|3[0-5])$"));
    const QStringList directions{QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("up"),
                                 QStringLiteral("down")};
    const bool hasModifier = std::any_of(tokens.begin(), tokens.end(), [&modifiers](const QString& token) {
        return modifiers.contains(token);
    });
    QStringList keys, words;
    for (const auto& token : tokens) {
        const bool isKey = modifiers.contains(token) || namedKeys.contains(token) || functionKey.match(token).hasMatch()
            || (hasModifier && (token.size() == 1 || directions.contains(token)));
        (isKey ? keys : words).append(token);
    }
    const auto triggers = row.value(QLatin1String("triggers")).toStringList();
    QStringList searchable{row.value(QLatin1String("label")).toString(),
                           row.value(QLatin1String("id")).toString(),
                           row.value(QLatin1String("description")).toString(),
                           row.value(QLatin1String("mode")).toString(),
                           row.value(QLatin1String("category")).toString(),
                           translated(group.label),
                           QString::fromLatin1(group.id)};
    if (triggers.isEmpty()) {
        searchable.append(QStringLiteral("unassigned"));
        searchable.append(translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Unassigned")));
    }
    const auto haystack = normalized(searchable.join(QLatin1Char(' ')));
    const auto wordsMatch = [&haystack, &words](const QString& trigger) {
        return std::all_of(words.begin(), words.end(), [&haystack, &trigger](const QString& word) {
            return haystack.contains(word) || trigger.contains(word);
        });
    };
    if (keys.isEmpty() && wordsMatch(QString()))
        return true;
    // Textual trigger descriptions (including portal and multimedia keys)
    // must follow the same single-alternative rule as recognized key names.
    // Never match Meta from one binding and VolumeDown from another.
    return std::any_of(triggers.begin(), triggers.end(), [&keys, &wordsMatch](const QString& trigger) {
        const auto normalizedTrigger = normalized(trigger);
        const auto keyParts = parts(normalizedTrigger);
        return std::all_of(keys.begin(), keys.end(),
                           [&keyParts](const QString& key) {
                               return keyParts.contains(key);
                           })
            && wordsMatch(normalizedTrigger);
    });
}

Group groupFor(const QVariantMap& row)
{
    const QString id = row.value(QLatin1String("id")).toString();
    if (row.value(QLatin1String("external")).toBool())
        return Shell;
    if (id.startsWith(QLatin1String("swap_virtual_screen_")) || id.startsWith(QLatin1String("rotate_virtual_screens_"))
        || row.value(QLatin1String("categoryOrder"), -1).toInt() == 8)
        return VirtualScreens;
    if (id.startsWith(QLatin1String("scroll_focus_tab_")) || id.startsWith(QLatin1String("scroll_cycle_tab"))
        || id == QLatin1String("scroll_toggle_column_tabbed"))
        return Tabs;
    if (id.startsWith(QLatin1String("focus_")) || id.startsWith(QLatin1String("cycle_window_"))
        || id.startsWith(QLatin1String("scroll_focus_")) || id == QLatin1String("scroll_switch_focus_float_tiling"))
        return Focus;
    if (id.startsWith(QLatin1String("move_window_")) || id.startsWith(QLatin1String("swap_window_"))
        || id.startsWith(QLatin1String("snap_to_zone_")) || id.startsWith(QLatin1String("rotate_windows_"))
        || id == QLatin1String("swap_master") || id == QLatin1String("toggle_window_float"))
        return Arrange;
    if (id.startsWith(QLatin1String("span_window_")) || id == QLatin1String("push_to_empty_zone"))
        return Zones;
    if (id == QLatin1String("restore_window_size") || id == QLatin1String("retile")
        || id.contains(QLatin1String("master_ratio")) || id.contains(QLatin1String("master_count")))
        return Sizing;
    if (id.startsWith(QLatin1String("scroll_center_")) || id.startsWith(QLatin1String("scroll_view_"))
        || id == QLatin1String("scroll_toggle_windowed_fullscreen"))
        return View;
    if (id.contains(QLatin1String("window_height")) || id == QLatin1String("scroll_expand_window"))
        return WindowSize;
    if (id.contains(QLatin1String("column_width")) || id == QLatin1String("scroll_maximize_column")
        || id == QLatin1String("scroll_maximize_to_edges") || id == QLatin1String("scroll_expand_column"))
        return ColumnSize;
    if (id.startsWith(QLatin1String("scroll_")))
        return Columns;
    if (row.value(QLatin1String("mode")).toString() == QLatin1String("layouts")
        || row.value(QLatin1String("categoryOrder"), -1).toInt() == 1 || id == QLatin1String("toggle_autotile")
        || id == QLatin1String("resnap_to_new_layout") || id == QLatin1String("snap_all_windows"))
        return Layouts;
    return General;
}

bool applies(const QVariantMap& row, Group group, const QString& scope, bool layoutsAvailable)
{
    const auto mode = row.value(QLatin1String("mode")).toString();
    if (mode == QLatin1String("layouts") && !layoutsAvailable)
        return false;
    if (scope == QLatin1String("all"))
        return true;
    if (scope == QLatin1String("shell"))
        return group == Shell;
    if (scope == QLatin1String("general"))
        return group == General || group == VirtualScreens;
    // The approved reference gives global app commands and virtual screens
    // their own General section, rather than duplicating them in every mode.
    if (group == General || group == VirtualScreens || group == Shell)
        return false;
    return mode == QLatin1String("all") || mode == QLatin1String("layouts") || mode == scope
        || (mode == QLatin1String("autotile") && scope == QLatin1String("tiling"))
        || (mode == QLatin1String("managed")
            && (scope == QLatin1String("tiling") || scope == QLatin1String("scrolling")));
}

QString fallbackDescription(const QVariantMap& row, Group group)
{
    const auto mode = row.value(QLatin1String("mode")).toString();
    if (mode == QLatin1String("autotile") || mode == QLatin1String("tiling"))
        return translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard",
                                            "Available with tiling algorithms that support a master area."));
    if (group == ColumnSize || group == WindowSize)
        return translated(
            QT_TRANSLATE_NOOP("PhosphorShellDashboard",
                              "Column size follows the strip’s axis. Window size follows the axis inside a column."));
    if (mode == QLatin1String("managed"))
        return translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Available in tiling and scrolling."));
    if (mode == QLatin1String("snapping"))
        return translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Available in snapping."));
    if (mode == QLatin1String("scrolling"))
        return translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Available in scrolling."));
    return translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Available in all placement modes."));
}

int familyFor(const QString& id)
{
    static const QStringList directions{QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("up"),
                                        QStringLiteral("down")};
    for (size_t i = 0; i < std::size(Families); ++i) {
        const auto prefix = QLatin1String(Families[i].prefix);
        if (!id.startsWith(prefix))
            continue;
        const auto suffix = id.mid(prefix.size());
        if ((Families[i].digits && suffix.size() == 1 && suffix >= QLatin1String("1") && suffix <= QLatin1String("9"))
            || (!Families[i].digits && directions.contains(suffix)))
            return static_cast<int>(i);
    }
    return -1;
}

QStringList compressedParts(const FamilySpec& family, const QVariantList& children)
{
    if (children.size() != (family.digits ? 9 : 4))
        return {};
    static const QHash<QString, Qt::Key> directions{{QStringLiteral("left"), Qt::Key_Left},
                                                    {QStringLiteral("right"), Qt::Key_Right},
                                                    {QStringLiteral("up"), Qt::Key_Up},
                                                    {QStringLiteral("down"), Qt::Key_Down}};
    Qt::KeyboardModifiers modifiers;
    QStringList prefix;
    for (qsizetype i = 0; i < children.size(); ++i) {
        const auto child = children[i].toMap();
        const auto triggers = child.value(QLatin1String("triggers")).toStringList();
        if (triggers.size() != 1)
            return {};
        const auto sequence = QKeySequence::fromString(triggers.first(), QKeySequence::PortableText);
        if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown)
            return {};
        const auto suffix = child.value(QLatin1String("id")).toString().mid(qstrlen(family.prefix));
        const auto expected = family.digits ? static_cast<Qt::Key>(Qt::Key_0 + suffix.toInt())
                                            : directions.value(suffix, Qt::Key_unknown);
        if (sequence[0].key() != expected)
            return {};
        if (i == 0) {
            modifiers = sequence[0].keyboardModifiers();
            prefix = parts(sequence.toString(QKeySequence::PortableText));
            prefix.removeLast();
        } else if (modifiers != sequence[0].keyboardModifiers()) {
            return {};
        }
    }
    prefix.append(family.digits ? QStringLiteral("1–9") : QStringLiteral("← → ↑ ↓"));
    return prefix;
}

int referenceOrder(const QString& id)
{
    // Presentation order follows the approved reference, independently of
    // the daemon's registration/category order. This table supplies no
    // bindings, labels or applicability; those remain the live catalog's.
    static const QHash<QString, int> order = [] {
        QHash<QString, int> result;
        int next = 0;
        for (const auto& family : Families) {
            const QString prefix = QString::fromLatin1(family.prefix);
            if (family.digits) {
                for (int number = 1; number <= 9; ++number)
                    result.insert(prefix + QString::number(number), next++);
            } else {
                for (const auto* direction : {"left", "right", "up", "down"})
                    result.insert(prefix + QString::fromLatin1(direction), next++);
            }
        }
        // Grouping happens before this order is applied, so the relative
        // positions of actions in different groups do not affect the view.
        constexpr const char* actions[] = {
            "focus_master",
            "swap_master",
            "increase_master_ratio",
            "decrease_master_ratio",
            "increase_master_count",
            "decrease_master_count",
            "toggle_window_float",
            "scroll_switch_focus_float_tiling",
            "restore_window_size",
            "retile",
            "push_to_empty_zone",
            "cycle_window_forward",
            "cycle_window_backward",
            "rotate_windows_clockwise",
            "rotate_windows_counterclockwise",
            "toggle_autotile",
            "layout_picker",
            "previous_layout",
            "next_layout",
            "toggle_layout_lock",
            "resnap_to_new_layout",
            "snap_all_windows",
            "open_editor",
            "open_settings",
            "toggle_cheatsheet",
            "rotate_virtual_screens_clockwise",
            "rotate_virtual_screens_counterclockwise",
            "scroll_focus_column_first",
            "scroll_focus_column_last",
            "scroll_focus_window_top",
            "scroll_focus_window_bottom",
            "scroll_focus_column_left",
            "scroll_focus_column_right",
            "scroll_focus_column_left_or_last",
            "scroll_focus_column_right_or_first",
            "scroll_move_column_to_first",
            "scroll_move_column_to_last",
            "scroll_consume_window",
            "scroll_expel_window",
            "scroll_consume_or_expel_left",
            "scroll_consume_or_expel_right",
            "scroll_move_to_floating",
            "scroll_move_to_tiling",
            "scroll_center_column",
            "scroll_center_visible_columns",
            "scroll_view_page_back",
            "scroll_view_page_forward",
            "scroll_toggle_windowed_fullscreen",
            "scroll_toggle_column_tabbed",
            "scroll_cycle_tab",
            "scroll_cycle_tab_back",
            "scroll_cycle_column_width",
            "scroll_cycle_column_width_back",
            "scroll_increase_column_width",
            "scroll_decrease_column_width",
            "scroll_maximize_column",
            "scroll_maximize_to_edges",
            "scroll_expand_column",
            "scroll_minimize_column_width",
            "scroll_equalize_column_widths",
            "scroll_cycle_window_height",
            "scroll_cycle_window_height_back",
            "scroll_increase_window_height",
            "scroll_decrease_window_height",
            "scroll_maximize_window_height",
            "scroll_expand_window",
            "scroll_minimize_window_height",
            "scroll_equalize_window_heights",
            "launcher.toggle",
            "dashboard.toggle",
            "control-center.toggle",
            "notify.toggle",
            "power.toggle",
            "picker.toggle",
            "lock.lock",
            "cheatsheet.toggle",
        };
        for (const auto* action : actions)
            result.insert(QString::fromLatin1(action), next++);
        return result;
    }();
    return order.value(id, -1);
}

bool referenceLess(const QVariant& left, const QVariant& right)
{
    const auto leftRow = left.toMap();
    const auto rightRow = right.toMap();
    const int leftOrder = referenceOrder(leftRow.value(QLatin1String("id")).toString());
    const int rightOrder = referenceOrder(rightRow.value(QLatin1String("id")).toString());
    if (leftOrder >= 0 && rightOrder >= 0)
        return leftOrder < rightOrder;
    if (leftOrder >= 0 || rightOrder >= 0)
        return leftOrder >= 0;
    // New daemon actions remain visible after the authored controls. Honor
    // their supplied rowOrder, retaining incoming order for equal/missing
    // values through stable_sort.
    constexpr int unspecified = (std::numeric_limits<int>::max)();
    return leftRow.value(QLatin1String("rowOrder"), unspecified).toInt()
        < rightRow.value(QLatin1String("rowOrder"), unspecified).toInt();
}

QVariantList shellRows()
{
    const std::pair<const char*, const char*> actions[] = {
        {"launcher.toggle", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Launcher")},
        {"dashboard.toggle", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Workspace overview")},
        {"control-center.toggle", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Quick settings")},
        {"notify.toggle", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Notification center")},
        {"power.toggle", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Session menu")},
        {"picker.toggle", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Appearance")},
        {"lock.lock", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Lock session")},
        {"cheatsheet.toggle", QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Shell shortcut reference")},
    };
    QVariantList rows;
    for (const auto& [id, label] : actions) {
        rows.append(QVariantMap{
            {QStringLiteral("id"), QString::fromLatin1(id)},
            {QStringLiteral("label"), translated(label)},
            {QStringLiteral("description"),
             translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard",
                                          "Assign this shell action in your compositor’s shortcut configuration."))},
            {QStringLiteral("mode"), QStringLiteral("shell")},
            {QStringLiteral("external"), true},
            {QStringLiteral("triggers"), QStringList{}}});
    }
    return rows;
}
} // namespace

ShortcutReferenceModel::ShortcutReferenceModel(QObject* parent)
    : QObject(parent)
{
    rebuild();
}

void ShortcutReferenceModel::setRows(const QVariantList& rows)
{
    if (m_rows == rows)
        return;
    m_rows = rows;
    rebuild();
    Q_EMIT rowsChanged();
}

void ShortcutReferenceModel::setScope(const QString& scope)
{
    static const QStringList valid{QStringLiteral("tiling"),  QStringLiteral("scrolling"), QStringLiteral("snapping"),
                                   QStringLiteral("general"), QStringLiteral("shell"),     QStringLiteral("all")};
    if (m_scope == scope || !valid.contains(scope))
        return;
    m_scope = scope;
    rebuild();
    Q_EMIT scopeChanged();
}

void ShortcutReferenceModel::setQuery(const QString& query)
{
    if (m_query == query)
        return;
    m_query = query;
    rebuild();
    Q_EMIT queryChanged();
}

void ShortcutReferenceModel::setAssignedOnly(bool assignedOnly)
{
    if (m_assignedOnly == assignedOnly)
        return;
    m_assignedOnly = assignedOnly;
    rebuild();
    Q_EMIT assignedOnlyChanged();
}

void ShortcutReferenceModel::setLayoutsAvailable(bool available)
{
    if (m_layoutsAvailable == available)
        return;
    m_layoutsAvailable = available;
    rebuild();
    Q_EMIT layoutsAvailableChanged();
}

QStringList ShortcutReferenceModel::keyPartsForTrigger(const QString& trigger) const
{
    auto result = parts(trigger);
    for (auto& part : result) {
        if (part == QLatin1String("Left"))
            part = QStringLiteral("←");
        else if (part == QLatin1String("Right"))
            part = QStringLiteral("→");
        else if (part == QLatin1String("Up"))
            part = QStringLiteral("↑");
        else if (part == QLatin1String("Down"))
            part = QStringLiteral("↓");
        else if (part == QLatin1String("Return") || part == QLatin1String("Enter"))
            part = translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Enter"));
        else if (part == QLatin1String("Escape"))
            part = translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Esc"));
        else if (part == QLatin1String("PgDown"))
            part = translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "PgDn"));
        else if (part == QLatin1String("Space"))
            part = translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Space"));
        else if (part == QLatin1String("Ctrl"))
            part = translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Ctrl"));
        else if (part == QLatin1String("Meta"))
            part = translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Meta"));
        else if (part == QLatin1String("Alt"))
            part = translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Alt"));
        else if (part == QLatin1String("Shift"))
            part = translated(QT_TRANSLATE_NOOP("PhosphorShellDashboard", "Shift"));
    }
    return result;
}

bool ShortcutReferenceModel::event(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        rebuild();
    return QObject::event(event);
}

void ShortcutReferenceModel::rebuild()
{
    std::array<QVariantList, std::size(Groups)> grouped;
    auto sourceRows = m_rows;
    // IPC verbs deliberately have no inferred compositor binding. A catalog
    // entry with the same name cannot override this external-command contract.
    const auto externalRows = shellRows();
    QSet<QString> seen;
    for (const auto& row : externalRows)
        seen.insert(row.toMap().value(QLatin1String("id")).toString());
    sourceRows.append(externalRows);
    int resultCount = 0;
    for (qsizetype i = 0; i < sourceRows.size(); ++i) {
        auto row = sourceRows[i].toMap();
        const auto id = row.value(QLatin1String("id")).toString();
        const bool shell = i >= m_rows.size();
        if (id.isEmpty() || (!shell && seen.contains(id)))
            continue;
        seen.insert(id);
        row.insert(QStringLiteral("external"), shell);
        auto triggers = row.value(QLatin1String("triggers")).toStringList();
        triggers.removeIf([](const QString& trigger) {
            return trigger.trimmed().isEmpty();
        });
        row.insert(QStringLiteral("triggers"), triggers);
        row.insert(QStringLiteral("assigned"), !triggers.isEmpty());
        row.insert(QStringLiteral("family"), false);
        row.insert(QStringLiteral("children"), QVariantList{});
        row.insert(QStringLiteral("keyParts"), QStringList{});
        if (m_scope == QLatin1String("scrolling")
            && !row.value(QLatin1String("templatesDescription")).toString().isEmpty())
            row.insert(QStringLiteral("description"), row.value(QLatin1String("templatesDescription")));
        if (!row.contains(QLatin1String("description")))
            row.insert(QStringLiteral("description"), QString());
        if (row.value(QLatin1String("label")).toString().isEmpty())
            row.insert(QStringLiteral("label"), id);
        QVariantList bindings;
        for (const auto& trigger : triggers)
            bindings.append(QVariant::fromValue(keyPartsForTrigger(trigger)));
        row.insert(QStringLiteral("bindings"), bindings);
        const auto group = groupFor(row);
        if (row.value(QLatin1String("description")).toString().isEmpty())
            row.insert(QStringLiteral("description"), fallbackDescription(row, group));
        if (!applies(row, group, m_scope, m_layoutsAvailable) || (m_assignedOnly && triggers.isEmpty())
            || !matches(row, m_query, Groups[group]))
            continue;
        grouped[group].append(row);
        ++resultCount;
    }
    QVariantList result;
    for (size_t index = 0; index < grouped.size(); ++index) {
        auto& rows = grouped[index];
        if (rows.isEmpty())
            continue;
        std::stable_sort(rows.begin(), rows.end(), referenceLess);
        QVariantList displayRows;
        QSet<int> families;
        for (const auto& value : rows) {
            const auto row = value.toMap();
            const auto familyIndex = familyFor(row.value(QLatin1String("id")).toString());
            if (familyIndex < 0 || !m_query.trimmed().isEmpty()) {
                displayRows.append(row);
                continue;
            }
            if (families.contains(familyIndex))
                continue;
            families.insert(familyIndex);
            QVariantList children;
            for (const auto& candidate : rows) {
                if (familyFor(candidate.toMap().value(QLatin1String("id")).toString()) == familyIndex)
                    children.append(candidate);
            }
            if (children.size() == 1) {
                displayRows.append(children.first());
                continue;
            }
            const auto& family = Families[familyIndex];
            auto summary = row;
            const QString familyId = QStringLiteral("family:") + QString::fromLatin1(family.prefix).chopped(1);
            summary.insert(QStringLiteral("id"), familyId);
            summary.insert(QStringLiteral("label"), translated(family.label));
            summary.insert(QStringLiteral("description"), QString());
            summary.insert(QStringLiteral("family"), true);
            summary.insert(QStringLiteral("children"), children);
            summary.insert(QStringLiteral("triggers"), QStringList{});
            summary.insert(QStringLiteral("bindings"), QVariantList{});
            // Translate modifier labels through the same display conversion
            // as individual keys. The range is display-only, never a trigger.
            auto keyParts = compressedParts(family, children);
            for (auto& part : keyParts) {
                const auto converted = keyPartsForTrigger(part);
                if (converted.size() == 1)
                    part = converted.first();
            }
            summary.insert(QStringLiteral("keyParts"), keyParts);
            summary.insert(QStringLiteral("assigned"),
                           std::any_of(children.begin(), children.end(), [](const QVariant& child) {
                               return child.toMap().value(QLatin1String("assigned")).toBool();
                           }));
            displayRows.append(summary);
        }
        const auto& spec = Groups[index];
        result.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(spec.id)},
                                  {QStringLiteral("label"), translated(spec.label)},
                                  {QStringLiteral("icon"), QString::fromLatin1(spec.icon)},
                                  {QStringLiteral("count"), rows.size()},
                                  {QStringLiteral("rows"), displayRows}});
    }
    const bool groupsDiffer = result != m_groups;
    const bool countDiffers = resultCount != m_count;
    // Publish one coherent snapshot before notifying. A QML handler can
    // change the query from groupsChanged and immediately rebuild again.
    // Updating count after that signal would overwrite the newer result.
    if (groupsDiffer)
        m_groups = result;
    if (countDiffers)
        m_count = resultCount;
    if (groupsDiffer)
        Q_EMIT groupsChanged();
    if (countDiffers)
        Q_EMIT countChanged();
}
} // namespace PhosphorShellDashboard
