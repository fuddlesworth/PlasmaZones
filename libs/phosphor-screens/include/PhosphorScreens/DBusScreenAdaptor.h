// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorscreens_export.h"

#include <QDBusAbstractAdaptor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QStringList>

#include <functional>

class QScreen;

namespace Phosphor::Screens {

class IConfigStore;
class ScreenManager;

/**
 * @brief Reusable D-Bus adaptor for the screen-topology surface.
 *
 * Abstract over the D-Bus interface NAME (subclasses set `Q_CLASSINFO`);
 * everything else — the Q_SLOTS that read/mutate screen topology, the
 * signal surface, the JSON round-trip for `setVirtualScreenConfig`, the
 * caches that keep `getScreenInfo` / effective-ID broadcasts cheap — is
 * host-agnostic.
 *
 * Hosts use this by declaring a thin subclass carrying just the
 * `Q_CLASSINFO("D-Bus Interface", "...")` they want to expose, and
 * injecting `ScreenManager*` + `IConfigStore*` via the setters.
 *
 * Lifetimes: pointers are non-owning. `ScreenManager` and `IConfigStore`
 * must outlive this adaptor.
 */
class PHOSPHORSCREENS_EXPORT DBusScreenAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    // No Q_CLASSINFO("D-Bus Interface", ...) — subclass sets the interface
    // name so different hosts can register against their own services.

public:
    explicit DBusScreenAdaptor(QObject* parent = nullptr);
    ~DBusScreenAdaptor() override;

    /// Wire the screen-topology service the adaptor queries. Null is
    /// valid (most methods return empty results) to simplify early
    /// wiring. Safe to call repeatedly.
    void setScreenManager(ScreenManager* manager);

    /// Wire the VS config store used by swap/rotate/setVirtualScreenConfig.
    /// Null disables the VS mutation path (methods log + return a stable
    /// rejection token).
    void setConfigStore(IConfigStore* store);

public Q_SLOTS:
    // Screen queries
    QStringList getScreens();
    QString getScreenInfo(const QString& screenId);
    QString getPrimaryScreen();
    QString getScreenId(const QString& connectorName);
    void setPrimaryScreenFromKWin(const QString& connectorName);
    QRect getAvailableGeometry(const QString& screenId);
    QRect getScreenGeometry(const QString& screenId);

    // Virtual screen management
    QString getVirtualScreenConfig(const QString& physicalScreenId);
    void setVirtualScreenConfig(const QString& physicalScreenId, const QString& configJson);
    QStringList getPhysicalScreens();
    QString getEffectiveScreenAt(int x, int y);

    /// Swap the region of @p currentVirtualScreenId with the adjacent sibling
    /// VS in the given @p direction (left/right/up/down) within the same
    /// physical monitor. No-op if the current id is not virtual or no sibling
    /// lies in that direction. All per-VS state (windows, layout, autotile)
    /// follows the new geometry automatically.
    /// @return Empty string on success, otherwise a stable rejection token
    ///         from VirtualScreenSwapper::reasonString() so callers can
    ///         distinguish failure modes without parsing logs.
    QString swapVirtualScreenInDirection(const QString& currentVirtualScreenId, const QString& direction);

    /// Rotate every VS region on @p physicalScreenId through a spatial
    /// clockwise ring order.
    /// @return Empty string on success, otherwise a stable rejection token
    ///         from VirtualScreenSwapper::reasonString().
    QString rotateVirtualScreens(const QString& physicalScreenId, bool clockwise);

Q_SIGNALS:
    void screenAdded(const QString& screenId);
    void screenRemoved(const QString& screenId);
    void screenGeometryChanged(const QString& screenId);
    void virtualScreensChanged(const QString& physicalScreenId);

protected:
    /// @return the wired ScreenManager pointer (may be null, including when
    /// the wired manager has since been destroyed — QPointer tracks that).
    /// Defined out-of-line so callers don't need the complete ScreenManager
    /// / IConfigStore types just to include this header.
    ScreenManager* screenManager() const;
    /// @return the wired IConfigStore pointer (may be null, QPointer-guarded).
    IConfigStore* configStore() const;

private:
    void handleScreenGeometryChanged(QScreen* screen, const QString& physId);
    void handleScreenRemoved(QScreen* removedScreen, QScreen* targetScreen, const QString& cachedId);
    bool emitForEffectiveScreens(const QString& physId, const std::function<void(const QString&)>& emitFn);
    void invalidateScreenInfoCache();
    void connectScreenManagerSignals(ScreenManager* mgr);
    void disconnectScreenManagerSignals(ScreenManager* mgr);
    void wireQGuiApplicationSignals();

    QString m_primaryScreenOverride;
    // QPointer-guarded so a ScreenManager / IConfigStore destroyed before
    // this adaptor (e.g. daemon tears down data members before QObject
    // children) safely reads as null rather than dangling. Prevents UAF in
    // the destructor's disconnect path.
    QPointer<ScreenManager> m_screenManager;
    QPointer<IConfigStore> m_configStore;

    QStringList m_lastEmittedEffectiveIds;
    QHash<QString, QStringList> m_cachedEffectiveIdsPerScreen;
    QHash<QString, QString> m_cachedScreenInfoJson;

    bool m_qGuiAppSignalsWired = false;
};

} // namespace Phosphor::Screens
