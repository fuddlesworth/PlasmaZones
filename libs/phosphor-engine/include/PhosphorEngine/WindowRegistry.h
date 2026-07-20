// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/IWindowRegistry.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <phosphorengine_export.h>
#include <QHash>
#include <QMultiHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

namespace PhosphorEngine {

struct WindowMetadata
{
    QString appId;
    QString desktopFile;
    QString title;
    QString windowRole{}; ///< X11 WM_WINDOW_ROLE; empty for Wayland-native windows
    int pid = 0; ///< process id; 0 = unknown
    int virtualDesktop = 0; ///< 1-based x11 desktop number; 0 = all desktops / unknown
    /// Full desktop list when the window spans SEVERAL (but not all) desktops;
    /// empty for the common single-desktop / sticky / unknown cases. When
    /// non-empty, virtualDesktop equals the first entry.
    QList<int> virtualDesktops{};
    QString activity{}; ///< activity UUID; empty = all activities / unknown
    PhosphorProtocol::WindowType windowType = PhosphorProtocol::WindowType::Unknown;

    // ── Extended window properties. The kwin-effect snapshots these at metadata-push
    // time so the daemon's window-rule resolvers (shouldFloatByRule /
    // shouldRestoreFloatedPosition) can match the same KWin-property / geometry
    // fields the effect path resolves live. std::optional so an absent value (the
    // compositor could not report it — e.g. no underlying KWin::Window) leaves the
    // corresponding WindowQuery field disengaged, keeping a predicate over it inert,
    // mirroring window_query.cpp's engage-only-when-known contract. ──
    std::optional<bool> isMinimized{};
    std::optional<bool> isFullscreen{};
    std::optional<bool> isSticky{}; ///< on all virtual desktops
    std::optional<bool> isMaximized{}; ///< MaximizeFull (both axes)
    std::optional<bool> isFocused{}; ///< focused at metadata-push time (point-in-time, NOT
                                     ///< refreshed on focus change) — the open-path Float /
                                     ///< RestorePosition resolvers read it at window-open, where
                                     ///< it is fresh; the effect path reads live isFocused for
                                     ///< continuously-evaluated border / opacity rules.
    std::optional<bool> isTransient{}; ///< dialog/utility/popup/menu/tooltip/splash family or has a transient parent
    std::optional<bool> isNotification{}; ///< notification / critical-notification / on-screen-display
    std::optional<bool> keepAbove{};
    std::optional<bool> keepBelow{};
    std::optional<bool> skipTaskbar{};
    std::optional<bool> skipPager{};
    std::optional<bool> skipSwitcher{};
    std::optional<bool> isModal{};
    std::optional<bool> hasDecoration{}; ///< server-side title-bar / border
    std::optional<bool> isResizable{};
    std::optional<bool> isMovable{}; ///< window can be moved
    std::optional<bool> isMaximizable{}; ///< window can be maximized
    std::optional<int> width{}; ///< frame width in px
    std::optional<int> height{}; ///< frame height in px
    std::optional<int> positionX{}; ///< frame left edge X in px
    std::optional<int> positionY{}; ///< frame top edge Y in px
    std::optional<QString> captionNormal{}; ///< title without the WM-added app-name suffix

    bool operator==(const WindowMetadata& other) const
    {
        return appId == other.appId && desktopFile == other.desktopFile && title == other.title
            && windowRole == other.windowRole && pid == other.pid && virtualDesktop == other.virtualDesktop
            && virtualDesktops == other.virtualDesktops && activity == other.activity && windowType == other.windowType
            && isMinimized == other.isMinimized && isFullscreen == other.isFullscreen && isSticky == other.isSticky
            && isMaximized == other.isMaximized && isFocused == other.isFocused && isTransient == other.isTransient
            && isNotification == other.isNotification && keepAbove == other.keepAbove && keepBelow == other.keepBelow
            && skipTaskbar == other.skipTaskbar && skipPager == other.skipPager && skipSwitcher == other.skipSwitcher
            && isModal == other.isModal && hasDecoration == other.hasDecoration && isResizable == other.isResizable
            && isMovable == other.isMovable && isMaximizable == other.isMaximizable && width == other.width
            && height == other.height && positionX == other.positionX && positionY == other.positionY
            && captionNormal == other.captionNormal;
    }
    bool operator!=(const WindowMetadata& other) const
    {
        return !(*this == other);
    }
};

class PHOSPHORENGINE_EXPORT WindowRegistry : public QObject, public IWindowRegistry
{
    Q_OBJECT

public:
    explicit WindowRegistry(QObject* parent = nullptr);
    ~WindowRegistry() override;

    void upsert(const QString& instanceId, const WindowMetadata& metadata);
    void remove(const QString& instanceId);

    std::optional<WindowMetadata> metadata(const QString& instanceId) const;
    /// The window's own context for per-window mode resolution: its virtual
    /// desktop (0 = all/unknown), the full desktop list when it spans several
    /// (empty otherwise), and activity (empty = all/unknown), read without
    /// copying the full ~30-field metadata record — this sits on the
    /// per-query float-resolver path. nullopt when the instance is unknown.
    struct WindowContext
    {
        int virtualDesktop = 0;
        QList<int> virtualDesktops;
        QString activity;
    };
    std::optional<WindowContext> windowContext(const QString& instanceId) const;
    Q_INVOKABLE QString appIdFor(const QString& instanceId) const override;
    QStringList instancesWithAppId(const QString& appId) const;
    bool contains(const QString& instanceId) const;
    QStringList allInstances() const;
    int size() const;
    void clear();

    Q_INVOKABLE QString canonicalizeWindowId(const QString& rawWindowId) override;
    Q_INVOKABLE QString canonicalizeForLookup(const QString& rawWindowId) const override;
    void releaseCanonical(const QString& anyWindowId);

    /// Defensive cleanup for windows that died WITHOUT a close signal reaching
    /// the registry (compositor crash, lost D-Bus call): drop every metadata
    /// record AND canonical-id translation whose instance id is absent from
    /// @p aliveInstanceIds, firing windowDisappeared for each so subscribers
    /// (e.g. saved-autotile-order cleanup) drop their ghost state too. Returns
    /// the count removed. @p aliveInstanceIds are the uuid components
    /// (WindowId::extractInstanceId), matching this registry's keying. The
    /// normal per-window path is remove() + releaseCanonical on windowClosed;
    /// this is the batch backstop the WTA alive-ids report drives.
    int pruneStaleInstances(const QSet<QString>& aliveInstanceIds);

Q_SIGNALS:
    void windowAppeared(const QString& instanceId);
    void metadataChanged(const QString& instanceId, const WindowMetadata& oldMetadata,
                         const WindowMetadata& newMetadata);
    void windowDisappeared(const QString& instanceId);

private:
    QHash<QString, WindowMetadata> m_records;
    QMultiHash<QString, QString> m_appIdIndex;
    QHash<QString, QString> m_canonicalByInstance;

    void indexInsert(const QString& instanceId, const QString& appId);
    void indexRemove(const QString& instanceId, const QString& appId);
};

} // namespace PhosphorEngine
