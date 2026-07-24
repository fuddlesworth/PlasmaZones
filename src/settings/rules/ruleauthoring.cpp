// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ruleauthoring.h"
#include "ruleauthoring_p.h"

#include "rulemodel.h"

#include "phosphor_i18n.h"

#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>

#include <QLatin1StringView>
#include <QList>
#include <QSet>

#include <array>

namespace PlasmaZones::RuleAuthoring {

namespace {

namespace ActionType = PhosphorRules::ActionType;
using PhosphorRules::Field;
using PhosphorRules::Operator;

/// Group a match Field into a picker category. The `Field` enum interleaves
/// state and context (e.g. IsMaximized sits after Activity), so the picker
/// groups by THIS classification, never by enum / emit order. The categories
/// are deliberately fine-grained: a single flat "State" bucket of ~19 entries
/// is hard to scan, so the window-kind, taskbar/switcher-hint, and
/// PlasmaZones-tiling concepts each get their own top-level fly-out. Items
/// within a category are sorted alphabetically by CategoryMenuButton; only the
/// returned order int controls the relative position of the categories. The
/// order ints below are assigned so the category labels sort alphabetically
/// (Context, Identity, Size, State, Taskbar & switcher, Tiling, Type).
PickerCategory fieldCategory(Field f)
{
    switch (f) {
    // Who the window is — identifiers a rule matches against.
    case Field::AppId:
    case Field::WindowClass:
    case Field::DesktopFile:
    case Field::WindowRole:
    case Field::Pid:
    case Field::Title:
    case Field::CaptionNormal:
        return {PhosphorI18n::tr("Identity"), 1};
    // What kind of window it is (its role/type), not a toggled runtime state.
    case Field::WindowType:
    case Field::IsTransient:
    case Field::IsModal:
    case Field::IsNotification:
        return {PhosphorI18n::tr("Type"), 6};
    // Live window-manager state and chrome flags.
    case Field::IsMaximized:
    case Field::IsMinimized:
    case Field::IsFullscreen:
    case Field::IsFocused:
    case Field::KeepAbove:
    case Field::KeepBelow:
    case Field::IsSticky:
    case Field::HasDecoration:
    case Field::IsResizable:
    case Field::IsMovable:
    case Field::IsMaximizable:
        return {PhosphorI18n::tr("State"), 3};
    // NETWM "skip" hints — whether the window opts out of the taskbar, pager,
    // or Alt+Tab switcher.
    case Field::SkipTaskbar:
    case Field::SkipPager:
    case Field::SkipSwitcher:
        return {PhosphorI18n::tr("Taskbar & switcher"), 4};
    // PlasmaZones-owned placement state.
    case Field::IsFloating:
    case Field::IsSnapped:
    case Field::IsTiled:
    case Field::Zone:
        return {PhosphorI18n::tr("Tiling"), 5};
    case Field::Width:
    case Field::Height:
    case Field::PositionX:
    case Field::PositionY:
        return {PhosphorI18n::tr("Size"), 2};
    case Field::ScreenId:
    case Field::VirtualDesktop:
    case Field::Activity:
    case Field::Mode:
    case Field::TiledWindowCount:
    case Field::ScreenOrientation:
    case Field::ActiveLayout:
        return {PhosphorI18n::tr("Context"), 0};
    }
    return {PhosphorI18n::tr("Other"), 99};
}

/// Short, user-facing help for one match Field — surfaced as the hover
/// tooltip on the leaf editor's info icon. Kept concise (one line); the
/// switch is exhaustive so every field, including the picker-hidden ones,
/// has a description.
QString fieldDescription(Field f)
{
    switch (f) {
    case Field::AppId:
        return PhosphorI18n::tr("The application's ID (Wayland app_id / desktop entry), e.g. org.kde.konsole.");
    case Field::WindowClass:
        return PhosphorI18n::tr("The window's class (WM_CLASS resource class), e.g. konsole.");
    case Field::DesktopFile:
        return PhosphorI18n::tr("The application's desktop entry file name.");
    case Field::WindowRole:
        return PhosphorI18n::tr("The window's X11 role (WM_WINDOW_ROLE). Empty for Wayland-native windows.");
    case Field::Pid:
        return PhosphorI18n::tr("The window's process ID.");
    case Field::Title:
        return PhosphorI18n::tr("The window's title-bar text.");
    case Field::WindowType:
        return PhosphorI18n::tr("The window's type (Normal, Dialog, Utility, Notification, …).");
    case Field::IsSticky:
        return PhosphorI18n::tr("Whether the window is shown on all virtual desktops.");
    case Field::IsFullscreen:
        return PhosphorI18n::tr("Whether the window is fullscreen.");
    case Field::IsMinimized:
        return PhosphorI18n::tr("Whether the window is minimized.");
    case Field::IsMaximized:
        return PhosphorI18n::tr("Whether the window is maximized.");
    case Field::IsFocused:
        return PhosphorI18n::tr("Whether the window currently has keyboard focus.");
    case Field::IsTransient:
        return PhosphorI18n::tr("Whether the window is a transient (a dialog or popup owned by another window).");
    case Field::IsNotification:
        return PhosphorI18n::tr("Whether the window is a notification or on-screen display.");
    case Field::Width:
        return PhosphorI18n::tr("The window's width in pixels.");
    case Field::Height:
        return PhosphorI18n::tr("The window's height in pixels.");
    case Field::KeepAbove:
        return PhosphorI18n::tr("Whether the window is set to stay above other windows (always on top).");
    case Field::KeepBelow:
        return PhosphorI18n::tr("Whether the window is set to stay below other windows.");
    case Field::SkipTaskbar:
        return PhosphorI18n::tr("Whether the window is hidden from the taskbar.");
    case Field::SkipPager:
        return PhosphorI18n::tr("Whether the window is hidden from the pager.");
    case Field::SkipSwitcher:
        return PhosphorI18n::tr("Whether the window is hidden from the window switcher (Alt+Tab).");
    case Field::IsModal:
        return PhosphorI18n::tr("Whether the window is a modal dialog.");
    case Field::HasDecoration:
        return PhosphorI18n::tr("Whether the window has a server-side title-bar and border.");
    case Field::IsResizable:
        return PhosphorI18n::tr("Whether the window can be resized.");
    case Field::IsMovable:
        return PhosphorI18n::tr("Whether the window can be moved.");
    case Field::IsMaximizable:
        return PhosphorI18n::tr("Whether the window can be maximized.");
    case Field::PositionX:
        return PhosphorI18n::tr("The window's left-edge X position in pixels.");
    case Field::PositionY:
        return PhosphorI18n::tr("The window's top-edge Y position in pixels.");
    case Field::CaptionNormal:
        return PhosphorI18n::tr("The window's title without the application-name suffix the window manager adds.");
    case Field::IsFloating:
        return PhosphorI18n::tr("Whether the window has been floated out of tiling (snap or autotile).");
    case Field::IsSnapped:
        return PhosphorI18n::tr(
            "Whether the window is snapped into a zone (manual-zone mode, where tiled windows are not snapped).");
    case Field::IsTiled:
        return PhosphorI18n::tr("Whether the window is managed by the autotile engine.");
    case Field::Zone:
        return PhosphorI18n::tr("The zone the window is snapped into (manual-zone mode only).");
    case Field::ScreenId:
        return PhosphorI18n::tr("The monitor the window is on.");
    case Field::VirtualDesktop:
        return PhosphorI18n::tr("The virtual desktop the window is on.");
    case Field::Activity:
        return PhosphorI18n::tr("The KDE Activity the window is on.");
    case Field::Mode:
        return PhosphorI18n::tr("The engine mode the window is placed by (snapping or tiling).");
    case Field::TiledWindowCount:
        return PhosphorI18n::tr(
            "How many windows are tiled on this monitor and desktop. Lets a rule switch the tiling algorithm as "
            "windows open and close, for example a centered single-window layout that gives way once a second window "
            "opens.");
    case Field::ScreenOrientation:
        return PhosphorI18n::tr(
            "Whether the monitor is in portrait or landscape orientation. Lets a rule pick a different layout or "
            "algorithm on a rotated screen.");
    case Field::ActiveLayout:
        return PhosphorI18n::tr(
            "The layout currently active on the monitor. Lets a rule change gaps, the overlay or the lock state for "
            "the screen showing a given layout. It cannot change which layout is assigned (that would be circular).");
    }
    return QString();
}

QString operatorLabelImpl(Operator op)
{
    switch (op) {
    case Operator::Equals:
        return PhosphorI18n::tr("is");
    case Operator::Contains:
        return PhosphorI18n::tr("contains");
    case Operator::StartsWith:
        return PhosphorI18n::tr("starts with");
    case Operator::EndsWith:
        return PhosphorI18n::tr("ends with");
    case Operator::Regex:
        return PhosphorI18n::tr("matches regex");
    case Operator::AppIdMatches:
        return PhosphorI18n::tr("matches app-id");
    case Operator::GreaterThan:
        return PhosphorI18n::tr("greater than");
    case Operator::LessThan:
        return PhosphorI18n::tr("less than");
    }
    // Wire-string fallback (same convention as paramLabel /
    // actionTypeFallbackLabel): a future operator missing a label entry
    // shows its raw token in the picker instead of a blank row.
    return PhosphorRules::operatorToString(op);
}

/// Single source of truth for WindowType → { int value, wire token, display label },
/// shared by matchFields() (the editor dropdown options) and windowTypeLabel() (the
/// collapsed rule-list summary). Order mirrors WindowTypeEnum.h — Unknown first as
/// the safe default, then Normal as the most common authoring choice.
struct WindowTypeOption
{
    int value;
    QString wire;
    QString label;
};
QList<WindowTypeOption> windowTypeOptions()
{
    struct Entry
    {
        PhosphorProtocol::WindowType type;
        QString label;
    };
    // CTAD deduces the array size from the brace-list, so a new enum value can't
    // silently drop the trailing entry by mismatching a hardcoded size.
    const std::array entries = std::to_array<Entry>({
        {PhosphorProtocol::WindowType::Unknown, PhosphorI18n::tr("Unknown")},
        {PhosphorProtocol::WindowType::Normal, PhosphorI18n::tr("Normal window")},
        {PhosphorProtocol::WindowType::Dialog, PhosphorI18n::tr("Dialog")},
        {PhosphorProtocol::WindowType::Utility, PhosphorI18n::tr("Utility")},
        {PhosphorProtocol::WindowType::Toolbar, PhosphorI18n::tr("Toolbar")},
        {PhosphorProtocol::WindowType::Splash, PhosphorI18n::tr("Splash screen")},
        {PhosphorProtocol::WindowType::Menu, PhosphorI18n::tr("Menu")},
        {PhosphorProtocol::WindowType::Tooltip, PhosphorI18n::tr("Tooltip")},
        {PhosphorProtocol::WindowType::Notification, PhosphorI18n::tr("Notification")},
        {PhosphorProtocol::WindowType::Dock, PhosphorI18n::tr("Dock / panel")},
        {PhosphorProtocol::WindowType::Desktop, PhosphorI18n::tr("Desktop")},
        {PhosphorProtocol::WindowType::OnScreenDisplay, PhosphorI18n::tr("On-screen display")},
        {PhosphorProtocol::WindowType::Popup, PhosphorI18n::tr("Popup")},
    });
    QList<WindowTypeOption> out;
    out.reserve(static_cast<int>(entries.size()));
    for (const auto& e : entries) {
        out.append({static_cast<int>(e.type), PhosphorProtocol::windowTypeToString(e.type), e.label});
    }
    return out;
}

/// Single source for the closed Mode / ScreenOrientation token vocabularies (the
/// stored value IS the wire token), shared by matchFields() (the editor dropdown
/// options) and modeLabel() / orientationLabel() (the collapsed rule-list summary),
/// so the picker and the summary can never drift.
struct ClosedTokenOption
{
    QString wire;
    QString label;
};
QList<ClosedTokenOption> modeOptions()
{
    return {{QStringLiteral("snapping"), PhosphorI18n::tr("Snapping")},
            {QStringLiteral("tiling"), PhosphorI18n::tr("Tiling")}};
}
QList<ClosedTokenOption> orientationOptions()
{
    return {{QStringLiteral("landscape"), PhosphorI18n::tr("Landscape")},
            {QStringLiteral("portrait"), PhosphorI18n::tr("Portrait")}};
}
QString closedTokenLabel(const QList<ClosedTokenOption>& opts, const QString& token)
{
    for (const ClosedTokenOption& o : opts) {
        if (o.wire == token) {
            return o.label;
        }
    }
    // Unknown token (hand-edited rule): round-trip verbatim.
    return token;
}

} // namespace

QString windowTypeLabel(int windowTypeValue)
{
    for (const WindowTypeOption& opt : windowTypeOptions()) {
        if (opt.value == windowTypeValue) {
            return opt.label;
        }
    }
    // Unknown value (hand-edited rule): show the raw int rather than a blank.
    return QString::number(windowTypeValue);
}

QString modeLabel(const QString& modeToken)
{
    return closedTokenLabel(modeOptions(), modeToken);
}

QString orientationLabel(const QString& orientationToken)
{
    return closedTokenLabel(orientationOptions(), orientationToken);
}

QString enumOptionLabel(const QString& type, const QString& key, const QString& wireValue)
{
    namespace ActionParam = PhosphorRules::ActionParam;
    if ((type == ActionType::SetEngineMode || type == ActionType::DisableEngine) && key == ActionParam::Mode) {
        if (wireValue == QLatin1String("snapping")) {
            return PhosphorI18n::tr("Snapping");
        }
        if (wireValue == QLatin1String("autotile")) {
            return PhosphorI18n::tr("Autotile");
        }
        if (wireValue == QLatin1String("scrolling")) {
            return PhosphorI18n::tr("Scrolling");
        }
    }
    if (type == ActionType::OverrideOverlayStyle && key == ActionParam::Value) {
        if (wireValue == PhosphorRules::OverlayStyleToken::Rectangles) {
            return PhosphorI18n::tr("Zone rectangles");
        }
        if (wireValue == PhosphorRules::OverlayStyleToken::Preview) {
            return PhosphorI18n::tr("Layout preview");
        }
    }
    if (type == ActionType::SetInsertPosition && key == ActionParam::Value) {
        if (wireValue == PhosphorRules::InsertPositionToken::End) {
            return PhosphorI18n::tr("End of stack");
        }
        if (wireValue == PhosphorRules::InsertPositionToken::AfterFocused) {
            return PhosphorI18n::tr("After focused window");
        }
        if (wireValue == PhosphorRules::InsertPositionToken::AsMaster) {
            return PhosphorI18n::tr("As master");
        }
    }
    if (type == ActionType::SetOverflowBehavior && key == ActionParam::Value) {
        if (wireValue == PhosphorRules::OverflowBehaviorToken::Float) {
            return PhosphorI18n::tr("Float overflow windows");
        }
        if (wireValue == PhosphorRules::OverflowBehaviorToken::Unlimited) {
            return PhosphorI18n::tr("Unlimited (no cap)");
        }
    }
    if (type == ActionType::SetDragBehavior && key == ActionParam::Value) {
        if (wireValue == PhosphorRules::DragBehaviorToken::Float) {
            return PhosphorI18n::tr("Float on drag");
        }
        if (wireValue == PhosphorRules::DragBehaviorToken::Reorder) {
            return PhosphorI18n::tr("Reorder in stack");
        }
    }
    if (type == ActionType::SetWindowLayer && key == ActionParam::Value) {
        if (wireValue == PhosphorRules::WindowLayerToken::Above) {
            return PhosphorI18n::tr("Above other windows");
        }
        if (wireValue == PhosphorRules::WindowLayerToken::Normal) {
            return PhosphorI18n::tr("Normal stacking");
        }
        if (wireValue == PhosphorRules::WindowLayerToken::Below) {
            return PhosphorI18n::tr("Below other windows");
        }
    }
    return wireValue;
}

QVariantList matchFields()
{
    // Pid and WindowRole are intentionally omitted from the picker —
    // both are footguns in a persistent rule store:
    //   * Pid is ephemeral. A `Pid equals 12345` predicate matches one
    //     specific process instance and is dead the moment that process
    //     restarts. Surfacing it in the picker invites users to author
    //     rules that silently stop working.
    //   * WindowRole is the X11 WM_WINDOW_ROLE property, empty for every
    //     Wayland-native window — PlasmaZones is Wayland-only (per
    //     CLAUDE.md), so the picker would always read as blank.
    // The Field enum keeps both values for back-compat with already-saved
    // rules; only the authoring UI hides them.
    //
    // Iterate the Field enum directly with a deny-set rather than a
    // hand-maintained allow-list: a new Field value (e.g. a hypothetical
    // future `MimeType`) auto-surfaces in the picker unless it's
    // explicitly hidden here. Mirrors the `userAuthorable` filter shape
    // that replaced `kTypes` in actionTypes() below.
    static const QSet<Field> kHiddenFields = {Field::Pid, Field::WindowRole};
    QVariantList out;
    for (int i = 0; i < PhosphorRules::FieldCount; ++i) {
        const auto f = static_cast<Field>(i);
        if (kHiddenFields.contains(f)) {
            continue;
        }
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(f);
        // The JSON wire string for this field — QML keys off this rather than
        // reconstructing the enum↔string table itself.
        entry[QStringLiteral("wire")] = PhosphorRules::fieldToString(f);
        entry[QStringLiteral("label")] = RuleModel::fieldLabel(f);
        const PickerCategory fcat = fieldCategory(f);
        entry[QStringLiteral("category")] = fcat.label;
        entry[QStringLiteral("categoryOrder")] = fcat.order;
        // One-line help surfaced as the leaf editor's info-icon tooltip.
        entry[QStringLiteral("description")] = fieldDescription(f);
        QString kind = QStringLiteral("string");
        if (f == Field::WindowType) {
            // WindowType is stored as the int underlying the
            // PhosphorProtocol::WindowType enum on the wire. A plain "number" SpinBox
            // left users with no idea what each value meant ("2" — Dialog? Utility?),
            // so a dedicated kind renders a dropdown. Options come from the single-source
            // windowTypeOptions() table (also used by the collapsed-summary label).
            kind = QStringLiteral("windowType");
            QVariantList options;
            for (const WindowTypeOption& opt : windowTypeOptions()) {
                QVariantMap option;
                option[QStringLiteral("value")] = opt.value;
                option[QStringLiteral("wire")] = opt.wire;
                option[QStringLiteral("label")] = opt.label;
                options.append(option);
            }
            entry[QStringLiteral("options")] = options;
        } else if (f == Field::VirtualDesktop) {
            // Numeric on the wire (a 1-based desktop number), but the QML editor
            // swaps the bare SpinBox for a desktop-picker ComboBox driven by
            // `settingsController.virtualDesktopNames`, so the user picks "2: Work"
            // rather than typing a number. Must precede the generic numeric branch.
            kind = QStringLiteral("virtualDesktop");
        } else if (PhosphorRules::fieldIsNumeric(f)) {
            kind = QStringLiteral("number");
        } else if (PhosphorRules::fieldIsBool(f)) {
            kind = QStringLiteral("bool");
        } else if (f == Field::ScreenId) {
            // QML editor swaps this for a screen-picker ComboBox driven by
            // `settingsController.screens`, so the user sees "LG Ultra HD" not
            // "LG Electronics:LG Ultra HD:115107/vs:0".
            kind = QStringLiteral("screen");
        } else if (f == Field::Activity) {
            // QML editor swaps this for an activity-picker ComboBox driven by
            // `settingsController.activities`, so the user sees the activity
            // name not its UUID.
            kind = QStringLiteral("activity");
        } else if (f == Field::Mode) {
            // Mode is string-valued on the wire (the placement-mode token), but
            // the vocabulary is closed — surface a dropdown of the friendly
            // tokens instead of a free-text box. The `options` carry
            // {value, wire, label}; unlike WindowType the value IS the wire
            // string (Mode is a string field, so the rule store keeps the token
            // verbatim).
            kind = QStringLiteral("mode");
            QVariantList options;
            for (const ClosedTokenOption& opt : modeOptions()) {
                QVariantMap option;
                option[QStringLiteral("value")] = opt.wire;
                option[QStringLiteral("wire")] = opt.wire;
                option[QStringLiteral("label")] = opt.label;
                options.append(option);
            }
            entry[QStringLiteral("options")] = options;
        } else if (f == Field::ScreenOrientation) {
            // String-valued on the wire (the orientation token), closed vocabulary
            // — a dropdown of the friendly tokens, same shape as Mode. The value IS
            // the wire token ("portrait" / "landscape"). Options come from the
            // single-source orientationOptions() table (also used by the summary).
            kind = QStringLiteral("orientation");
            QVariantList options;
            for (const ClosedTokenOption& opt : orientationOptions()) {
                QVariantMap option;
                option[QStringLiteral("value")] = opt.wire;
                option[QStringLiteral("wire")] = opt.wire;
                option[QStringLiteral("label")] = opt.label;
                options.append(option);
            }
            entry[QStringLiteral("options")] = options;
        } else if (f == Field::ActiveLayout) {
            // The value is a layout id (snap UUID or "autotile:<algo>"). The QML
            // editor swaps this for a layout-picker ComboBox driven by
            // `settingsController.layouts` (like the screen / activity pickers), so
            // the user picks a friendly name while the wire value stays the id.
            kind = QStringLiteral("layout");
        }
        entry[QStringLiteral("valueKind")] = kind;
        out.append(entry);
    }
    return out;
}

QVariantList operatorsForField(int fieldValue)
{
    // Bounded cast: QML hands us a raw int, and an out-of-range value must
    // not reach the Field classifiers (matchFields() bounds the same way).
    if (fieldValue < 0 || fieldValue >= PhosphorRules::FieldCount) {
        return {};
    }
    const Field field = static_cast<Field>(fieldValue);
    QList<Operator> ops;
    if (field == Field::Mode || field == Field::ScreenOrientation || field == Field::ActiveLayout) {
        // These are string-valued but their vocabulary is a closed single-select
        // dropdown (placement mode, portrait/landscape, a concrete layout id) — only
        // an exact-token Equals is meaningful. A substring / regex against a closed
        // token set (or a layout UUID) is a footgun the picker cannot author
        // sensibly. Mirrors the WindowType enum treatment.
        ops = {Operator::Equals};
    } else if (PhosphorRules::fieldIsString(field)) {
        ops = {Operator::Equals, Operator::Contains, Operator::StartsWith, Operator::EndsWith, Operator::Regex};
        if (field == Field::AppId) {
            ops.append(Operator::AppIdMatches);
        }
    } else if (PhosphorRules::fieldIsNumeric(field)) {
        ops = {Operator::Equals, Operator::GreaterThan, Operator::LessThan};
    } else if (PhosphorRules::fieldIsBool(field) || field == Field::WindowType) {
        ops = {Operator::Equals};
    }
    QVariantList out;
    for (Operator op : ops) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = static_cast<int>(op);
        // The JSON wire string for this operator — same contract as matchFields.
        entry[QStringLiteral("wire")] = PhosphorRules::operatorToString(op);
        entry[QStringLiteral("label")] = operatorLabelImpl(op);
        out.append(entry);
    }
    return out;
}

QVariantList allOperators()
{
    // Iterate the whole Operator enum via OperatorCount rather than a
    // hand-maintained list — a new operator auto-surfaces here (and so widens
    // the leaf editor's operator-column sizing) the moment it's added.
    QVariantList out;
    for (int i = 0; i < PhosphorRules::OperatorCount; ++i) {
        const auto op = static_cast<Operator>(i);
        QVariantMap entry;
        entry[QStringLiteral("value")] = i;
        entry[QStringLiteral("wire")] = PhosphorRules::operatorToString(op);
        entry[QStringLiteral("label")] = operatorLabelImpl(op);
        out.append(entry);
    }
    return out;
}

QString matchValueHint(const QString& op)
{
    // Keyed on the operator wire token: only the operators whose value editor is
    // a plain text box AND whose accepted syntax / matching semantics aren't
    // obvious get a hint. equals / contains / starts-with / ends-with are
    // self-explanatory; the picker / spin-box operators have no free-text field
    // to annotate. The match-side counterpart to the action-side paramHint.
    if (op == PhosphorRules::operatorToString(Operator::Regex)) {
        return PhosphorI18n::tr("Regular expression, e.g. ^(firefox|chromium)$");
    }
    if (op == PhosphorRules::operatorToString(Operator::AppIdMatches)) {
        return PhosphorI18n::tr("Matches by reverse-DNS segments, so “firefox” also matches “org.mozilla.firefox”.");
    }
    return {};
}

} // namespace PlasmaZones::RuleAuthoring
