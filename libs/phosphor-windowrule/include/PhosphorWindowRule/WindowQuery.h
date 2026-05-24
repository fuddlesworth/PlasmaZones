// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/WindowTypeEnum.h>

#include <QString>
#include <QVariant>

#include <optional>

#include "MatchTypes.h"

namespace PhosphorWindowRule {

/**
 * @brief The evaluation input — an attribute bag matched against a
 *        MatchExpression.
 *
 * Window attributes are `std::optional`: absent when evaluating a windowless
 * context query (zone-assignment resolution). A predicate over an absent
 * window field evaluates `false`, so window-property rules are naturally
 * inert during context resolution — no special-casing in the evaluator.
 *
 * Context attributes are always present. `virtualDesktop == 0` means "all
 * desktops" (a sticky window); empty `activity` means "all activities".
 */
struct WindowQuery
{
    // ── Window attributes — absent during windowless context resolution ──
    std::optional<QString> appId;
    std::optional<QString> windowClass;
    std::optional<QString> title;
    std::optional<QString> windowRole;
    std::optional<QString> desktopFile;
    std::optional<int> pid;
    std::optional<PhosphorProtocol::WindowType> windowType;
    std::optional<bool> isSticky;
    std::optional<bool> isFullscreen;
    std::optional<bool> isMinimized;
    std::optional<bool> isMaximized;

    // ── Context attributes — always present ──
    QString screenId;
    int virtualDesktop = 0; ///< 0 = all desktops
    QString activity; ///< empty = all activities

    /// True if any window attribute is set — i.e. this is a per-window query
    /// rather than a windowless context query.
    bool hasWindow() const
    {
        return appId.has_value() || windowClass.has_value() || title.has_value() || windowRole.has_value()
            || desktopFile.has_value() || pid.has_value() || windowType.has_value() || isSticky.has_value()
            || isFullscreen.has_value() || isMinimized.has_value() || isMaximized.has_value();
    }

    /**
     * @brief Resolve @p field to its current value.
     *
     * Returns an engaged QVariant for present attributes; an empty
     * `std::optional` for an absent window attribute (which a predicate
     * treats as a non-match). Context fields always resolve.
     *
     * Bool flags resolve to `bool`, numeric fields to `int`, the window
     * type to its underlying `int` (so `Equals`/`In` compare uniformly),
     * everything else to `QString`.
     */
    std::optional<QVariant> valueForField(Field field) const
    {
        switch (field) {
        case Field::AppId:
            return appId ? std::optional<QVariant>(*appId) : std::nullopt;
        case Field::WindowClass:
            return windowClass ? std::optional<QVariant>(*windowClass) : std::nullopt;
        case Field::DesktopFile:
            return desktopFile ? std::optional<QVariant>(*desktopFile) : std::nullopt;
        case Field::WindowRole:
            return windowRole ? std::optional<QVariant>(*windowRole) : std::nullopt;
        case Field::Title:
            return title ? std::optional<QVariant>(*title) : std::nullopt;
        case Field::Pid:
            return pid ? std::optional<QVariant>(*pid) : std::nullopt;
        case Field::WindowType:
            return windowType ? std::optional<QVariant>(static_cast<int>(*windowType)) : std::nullopt;
        case Field::IsSticky:
            return isSticky ? std::optional<QVariant>(*isSticky) : std::nullopt;
        case Field::IsFullscreen:
            return isFullscreen ? std::optional<QVariant>(*isFullscreen) : std::nullopt;
        case Field::IsMinimized:
            return isMinimized ? std::optional<QVariant>(*isMinimized) : std::nullopt;
        case Field::ScreenId:
            return std::optional<QVariant>(screenId);
        case Field::VirtualDesktop:
            return std::optional<QVariant>(virtualDesktop);
        case Field::Activity:
            return std::optional<QVariant>(activity);
        case Field::IsMaximized:
            return isMaximized ? std::optional<QVariant>(*isMaximized) : std::nullopt;
        }
        return std::nullopt;
    }
};

} // namespace PhosphorWindowRule
