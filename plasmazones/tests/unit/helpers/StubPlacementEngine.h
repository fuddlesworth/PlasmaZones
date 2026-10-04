// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file StubPlacementEngine.h
 * @brief A tiling-side placement engine whose answers a test sets directly,
 *        recording what the adaptor asks of it. It stands in for the autotile
 *        or scroll engine where only the adaptor's routing is under test.
 *
 * No Q_OBJECT: it declares no signals of its own, and a test emits the base
 * class's (windowOutputMoveExpected, windowFloatingChanged) directly.
 */

#include <PhosphorEngine/IPlacementState.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowPlacement.h>

#include <QHash>
#include <QList>
#include <QPair>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

class StubPlacementEngine : public PhosphorEngine::PlacementEngineBase
{
public:
    explicit StubPlacementEngine(QObject* parent = nullptr)
        : PhosphorEngine::PlacementEngineBase(parent)
    {
    }

    QString id; ///< engineId()
    QRect managedRect; ///< answered by lastManagedRect for every window
    QHash<QString, QString> heldScreen; ///< windowId -> the screen it is held on (current context)
    QSet<QString> trackedElsewhere; ///< tracked only in another (desktop, activity) context
    QString captureState; ///< when set, capturePlacement answers a slot in this state for a tracked window
    QSet<QString> activeScreens; ///< screens this engine runs
    QList<QPair<QString, QString>> releasedOffScreen; ///< (windowId, keepScreenId) per call
    QStringList handoffReleased; ///< windowIds handed off
    QList<HandoffContext> received; ///< handoffReceive contexts, in order

    QString engineId() const override
    {
        return id;
    }
    QRect lastManagedRect(const QString&) const override
    {
        return managedRect;
    }
    std::optional<PhosphorEngine::WindowPlacement> capturePlacement(const QString& windowId) const override
    {
        if (captureState.isEmpty() || !isWindowTracked(windowId)) {
            return std::nullopt;
        }
        PhosphorEngine::WindowPlacement placement;
        placement.windowId = windowId;
        placement.appId = windowId.section(QLatin1Char('|'), 0, 0);
        placement.screenId = heldScreen.value(windowId);
        PhosphorEngine::EngineSlot slot;
        slot.state = captureState;
        slot.order = 0;
        placement.engines.insert(id, slot);
        return placement;
    }
    void releaseWindowOffScreen(const QString& windowId, const QString& keepScreenId) override
    {
        releasedOffScreen.append({windowId, keepScreenId});
    }
    QString screenForTrackedWindow(const QString& windowId) const override
    {
        return heldScreen.value(windowId);
    }
    QString heldScreenForWindow(const QString& windowId) const override
    {
        return heldScreen.value(windowId);
    }
    bool isWindowTracked(const QString& windowId) const override
    {
        return heldScreen.contains(windowId) || trackedElsewhere.contains(windowId);
    }
    void handoffRelease(const QString& windowId) override
    {
        handoffReleased.append(windowId);
        heldScreen.remove(windowId);
    }
    void handoffReceive(const HandoffContext& ctx) override
    {
        received.append(ctx);
        heldScreen.insert(ctx.windowId, ctx.toScreenId);
    }

    bool isActiveOnScreen(const QString& screenId) const override
    {
        return activeScreens.contains(screenId);
    }
    void windowOpened(const QString&, const QString&, int, int) override
    {
    }
    void windowClosed(const QString&) override
    {
    }
    void windowFocused(const QString&, const QString&) override
    {
    }
    void toggleWindowFloat(const QString&, const QString&) override
    {
    }
    void setWindowFloat(const QString&, bool, const QString&) override
    {
    }
    void focusInDirection(const QString&, const PhosphorEngine::NavigationContext&) override
    {
    }
    void moveFocusedInDirection(const QString&, const PhosphorEngine::NavigationContext&) override
    {
    }
    void swapFocusedInDirection(const QString&, const PhosphorEngine::NavigationContext&) override
    {
    }
    void moveFocusedToPosition(int, const PhosphorEngine::NavigationContext&) override
    {
    }
    void rotateWindows(bool, const PhosphorEngine::NavigationContext&) override
    {
    }
    void reapplyLayout(const PhosphorEngine::NavigationContext&) override
    {
    }
    void snapAllWindows(const PhosphorEngine::NavigationContext&) override
    {
    }
    void cycleFocus(bool, const PhosphorEngine::NavigationContext&) override
    {
    }
    void pushToEmptyZone(const PhosphorEngine::NavigationContext&) override
    {
    }
    void restoreFocusedWindow(const PhosphorEngine::NavigationContext&) override
    {
    }
    void toggleFocusedFloat(const PhosphorEngine::NavigationContext&) override
    {
    }
    void saveState() override
    {
    }
    void loadState() override
    {
    }
    PhosphorEngine::IPlacementState* stateForScreen(const QString&) override
    {
        return nullptr;
    }
    const PhosphorEngine::IPlacementState* stateForScreen(const QString&) const override
    {
        return nullptr;
    }
};
