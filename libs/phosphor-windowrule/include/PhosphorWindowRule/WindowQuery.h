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
    std::optional<bool> isFocused;
    std::optional<bool> isTransient; ///< dialog/utility/popup/menu/tooltip/splash family, or has a transient parent
    std::optional<bool> isNotification; ///< notification / critical-notification / on-screen-display surface
    std::optional<int> width; ///< frame width in px
    std::optional<int> height; ///< frame height in px
    std::optional<bool> keepAbove; ///< window set to stay above others
    std::optional<bool> keepBelow; ///< window set to stay below others
    std::optional<bool> skipTaskbar; ///< hidden from the taskbar
    std::optional<bool> skipPager; ///< hidden from the pager
    std::optional<bool> skipSwitcher; ///< hidden from the window switcher
    std::optional<bool> isModal; ///< modal dialog
    std::optional<bool> hasDecoration; ///< has a server-side title-bar / border
    std::optional<bool> isResizable; ///< window can be resized
    std::optional<int> positionX; ///< frame left edge X in px
    std::optional<int> positionY; ///< frame top edge Y in px
    std::optional<QString> captionNormal; ///< title without the WM-added app-name suffix
    std::optional<bool> isFloating; ///< floated out of tiling (snap or autotile)
    std::optional<bool> isSnapped; ///< occupies a snap zone (snap mode only)
    std::optional<QString> zone; ///< the snap zone's UUID the window occupies

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
            || isFullscreen.has_value() || isMinimized.has_value() || isMaximized.has_value() || isFocused.has_value()
            || isTransient.has_value() || isNotification.has_value() || width.has_value() || height.has_value()
            || keepAbove.has_value() || keepBelow.has_value() || skipTaskbar.has_value() || skipPager.has_value()
            || skipSwitcher.has_value() || isModal.has_value() || hasDecoration.has_value() || isResizable.has_value()
            || positionX.has_value() || positionY.has_value() || captionNormal.has_value() || isFloating.has_value()
            || isSnapped.has_value() || zone.has_value();
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
        case Field::IsFocused:
            return isFocused ? std::optional<QVariant>(*isFocused) : std::nullopt;
        case Field::IsTransient:
            return isTransient ? std::optional<QVariant>(*isTransient) : std::nullopt;
        case Field::IsNotification:
            return isNotification ? std::optional<QVariant>(*isNotification) : std::nullopt;
        case Field::Width:
            return width ? std::optional<QVariant>(*width) : std::nullopt;
        case Field::Height:
            return height ? std::optional<QVariant>(*height) : std::nullopt;
        case Field::KeepAbove:
            return keepAbove ? std::optional<QVariant>(*keepAbove) : std::nullopt;
        case Field::KeepBelow:
            return keepBelow ? std::optional<QVariant>(*keepBelow) : std::nullopt;
        case Field::SkipTaskbar:
            return skipTaskbar ? std::optional<QVariant>(*skipTaskbar) : std::nullopt;
        case Field::SkipPager:
            return skipPager ? std::optional<QVariant>(*skipPager) : std::nullopt;
        case Field::SkipSwitcher:
            return skipSwitcher ? std::optional<QVariant>(*skipSwitcher) : std::nullopt;
        case Field::IsModal:
            return isModal ? std::optional<QVariant>(*isModal) : std::nullopt;
        case Field::HasDecoration:
            return hasDecoration ? std::optional<QVariant>(*hasDecoration) : std::nullopt;
        case Field::IsResizable:
            return isResizable ? std::optional<QVariant>(*isResizable) : std::nullopt;
        case Field::PositionX:
            return positionX ? std::optional<QVariant>(*positionX) : std::nullopt;
        case Field::PositionY:
            return positionY ? std::optional<QVariant>(*positionY) : std::nullopt;
        case Field::CaptionNormal:
            return captionNormal ? std::optional<QVariant>(*captionNormal) : std::nullopt;
        case Field::IsFloating:
            return isFloating ? std::optional<QVariant>(*isFloating) : std::nullopt;
        case Field::IsSnapped:
            return isSnapped ? std::optional<QVariant>(*isSnapped) : std::nullopt;
        case Field::Zone:
            return zone ? std::optional<QVariant>(*zone) : std::nullopt;
        }
        return std::nullopt;
    }
};

} // namespace PhosphorWindowRule
