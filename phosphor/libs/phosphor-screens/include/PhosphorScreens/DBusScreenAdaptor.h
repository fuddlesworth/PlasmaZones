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

namespace PhosphorScreens {

class IConfigStore;
class ScreenManager;

/**
 * @brief D-Bus adaptor for the `org.plasmazones.Screen` screen-topology
 *        surface: screen queries, virtual-screen mutation, the JSON
 *        round-trip for `setVirtualScreenConfig`, caches, and signals.
 *
 * The D-Bus interface name is declared HERE via `Q_CLASSINFO`, on the
 * class that declares the signals — not on a host subclass. Qt derives a
 * signal's D-Bus interface from the metaobject that *declares* the signal:
 * a subclass-only `Q_CLASSINFO` fixes incoming method dispatch (that walks
 * the whole adaptor hierarchy) but leaves every signal in this class
 * emitting on an auto-generated junk interface name, which no consumer's
 * match rule ever sees. The bus service name and object path — the parts
 * that genuinely vary per host — are still chosen by the host at
 * `registerObject` time.
 *
 * Lifetimes: pointers are non-owning. `ScreenManager` and `IConfigStore`
 * must outlive this adaptor.
 */
class PHOSPHORSCREENS_EXPORT DBusScreenAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Screen")

public:
    /// Primary constructor: wires the screen-topology service and the VS
    /// config store at construction time. @p manager and @p store may be
    /// null for bring-up paths that don't need either (most methods return
    /// empty results / rejection tokens in that case).
    explicit DBusScreenAdaptor(ScreenManager* manager, IConfigStore* store, QObject* parent = nullptr);
    ~DBusScreenAdaptor() override;

public Q_SLOTS:
    // Screen queries
    QStringList getScreens();
    QString getScreenInfo(const QString& screenId);
    QString getPrimaryScreen();
    QString getScreenId(const QString& connectorName);
    void setPrimaryScreenFromKWin(const QString& connectorName);
    /// Record the authoritative per-screen available geometry as reported by
    /// the compositor (the KWin effect's `clientArea(MaximizeArea)` query).
    /// Overrides the panel-strut heuristic in ScreenManager. @p screenName is
    /// a connector name (e.g. `DP-1`); @p x / @p y / @p width / @p height are
    /// the work-area rect. An invalid or zero-size rect clears the override —
    /// a defensive contract; the effect itself always reports a valid rect.
    void setAvailableGeometryFromKWin(const QString& screenName, int x, int y, int width, int height);
    QRect getAvailableGeometry(const QString& screenId);
    QRect getScreenGeometry(const QString& screenId);

    // Virtual screen management
    QString getVirtualScreenConfig(const QString& physicalScreenId);
    /// Set the virtual screen subdivision configuration for a physical screen.
    /// @return Empty string on success, otherwise a stable rejection token
    ///         (e.g. `parse_error`, `missing_screens`, `bad_index`,
    ///         `store_rejected`) so D-Bus callers can distinguish failure
    ///         modes without parsing logs. Matches the pattern used by
    ///         @ref swapVirtualScreenInDirection / @ref rotateVirtualScreens.
    QString setVirtualScreenConfig(const QString& physicalScreenId, const QString& configJson);
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
    /// Re-key the adaptor's per-screen caches from @p oldId to @p newId and
    /// surface the flip over D-Bus as a retract-of-old + announce-of-new pair.
    /// Called from @ref ScreenManager::screenIdentifierChanged — wired in
    /// @ref connectScreenManagerSignals. Without this propagation, D-Bus
    /// consumers tracking the screen by identifier would silently hold a
    /// dead ID after same-model hotplug disambiguation flips, and the
    /// adaptor's own @c m_cachedEffectiveIdsPerScreen entry at the old key
    /// would leak until the next screen-removal cleaned it up opportunistically.
    void handleScreenIdentifierChanged(const QString& oldId, const QString& newId);
    bool emitForEffectiveScreens(const QString& physId, const std::function<void(const QString&)>& emitFn);
    void invalidateScreenInfoCache();
    void connectScreenManagerSignals(ScreenManager* mgr);
    void disconnectScreenManagerSignals(ScreenManager* mgr);
    void wireQGuiApplicationSignals();
    /// Per-screen connect(geometryChanged) + connect(qGuiApp::screenRemoved)
    /// pair. Called both from the existing-screens loop in
    /// wireQGuiApplicationSignals and from the screenAdded handler so the
    /// wiring details can't drift between the two paths.
    void wirePerScreenSignals(QScreen* screen);

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

} // namespace PhosphorScreens
