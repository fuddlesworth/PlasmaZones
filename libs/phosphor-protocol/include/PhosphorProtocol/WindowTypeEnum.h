// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1StringView>
#include <QString>
#include <QStringView>

#include <optional>

namespace PhosphorProtocol {

/// Window-type vocabulary shared by the compositor effect, the daemon's
/// WindowRegistry, and (from the window-rule refactor onward) the window-rule
/// match engine. The compositor maps KWin's overlapping window-type predicates
/// onto exactly one of these — most-specific-first — then the value crosses
/// D-Bus as its underlying int and the daemon stores it.
///
/// Deliberately free of QtDBus / Q_DECLARE_METATYPE so the GPL KWin effect can
/// include this header without pulling in marshalling machinery.
enum class WindowType : int {
    Unknown = 0, ///< type could not be determined
    Normal = 1, ///< ordinary application toplevel
    Dialog = 2,
    Utility = 3,
    Toolbar = 4,
    Splash = 5,
    Menu = 6, ///< menu / popup menu / dropdown menu
    Tooltip = 7,
    Notification = 8,
    Dock = 9, ///< panels
    Desktop = 10,
    OnScreenDisplay = 11,
    Popup = 12, ///< generic override-redirect popup
};

/// Inclusive bounds of the valid WindowType underlying values — used to
/// range-check an int that crossed D-Bus before casting it back.
inline constexpr int windowTypeMinValue = static_cast<int>(WindowType::Unknown);
inline constexpr int windowTypeMaxValue = static_cast<int>(WindowType::Popup);

/// True if @p value is a valid WindowType underlying value.
inline bool isValidWindowType(int value)
{
    return value >= windowTypeMinValue && value <= windowTypeMaxValue;
}

/// Cast an int that crossed D-Bus to a WindowType; out-of-range values
/// (version skew, a malformed caller) fall back to WindowType::Unknown.
inline WindowType windowTypeFromInt(int value)
{
    return isValidWindowType(value) ? static_cast<WindowType>(value) : WindowType::Unknown;
}

/// Canonical lowercase wire string for a WindowType.
inline QString windowTypeToString(WindowType type)
{
    switch (type) {
    case WindowType::Unknown:
        return QStringLiteral("unknown");
    case WindowType::Normal:
        return QStringLiteral("normal");
    case WindowType::Dialog:
        return QStringLiteral("dialog");
    case WindowType::Utility:
        return QStringLiteral("utility");
    case WindowType::Toolbar:
        return QStringLiteral("toolbar");
    case WindowType::Splash:
        return QStringLiteral("splash");
    case WindowType::Menu:
        return QStringLiteral("menu");
    case WindowType::Tooltip:
        return QStringLiteral("tooltip");
    case WindowType::Notification:
        return QStringLiteral("notification");
    case WindowType::Dock:
        return QStringLiteral("dock");
    case WindowType::Desktop:
        return QStringLiteral("desktop");
    case WindowType::OnScreenDisplay:
        return QStringLiteral("onscreendisplay");
    case WindowType::Popup:
        return QStringLiteral("popup");
    }
    return QStringLiteral("unknown");
}

/// Strict parse: an unknown token returns nullopt so callers can drop the
/// input rather than silently coercing typos / future values to a default.
inline std::optional<WindowType> windowTypeFromString(QStringView s)
{
    static constexpr std::pair<QLatin1StringView, WindowType> kTable[] = {
        {QLatin1StringView("unknown"), WindowType::Unknown},
        {QLatin1StringView("normal"), WindowType::Normal},
        {QLatin1StringView("dialog"), WindowType::Dialog},
        {QLatin1StringView("utility"), WindowType::Utility},
        {QLatin1StringView("toolbar"), WindowType::Toolbar},
        {QLatin1StringView("splash"), WindowType::Splash},
        {QLatin1StringView("menu"), WindowType::Menu},
        {QLatin1StringView("tooltip"), WindowType::Tooltip},
        {QLatin1StringView("notification"), WindowType::Notification},
        {QLatin1StringView("dock"), WindowType::Dock},
        {QLatin1StringView("desktop"), WindowType::Desktop},
        {QLatin1StringView("onscreendisplay"), WindowType::OnScreenDisplay},
        {QLatin1StringView("popup"), WindowType::Popup},
    };
    for (const auto& [token, type] : kTable) {
        if (s.compare(token, Qt::CaseInsensitive) == 0) {
            return type;
        }
    }
    return std::nullopt;
}

} // namespace PhosphorProtocol
