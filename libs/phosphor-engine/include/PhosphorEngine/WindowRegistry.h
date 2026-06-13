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
    QString activity{}; ///< activity UUID; empty = all activities / unknown
    PhosphorProtocol::WindowType windowType = PhosphorProtocol::WindowType::Unknown;

    bool operator==(const WindowMetadata& other) const
    {
        return appId == other.appId && desktopFile == other.desktopFile && title == other.title
            && windowRole == other.windowRole && pid == other.pid && virtualDesktop == other.virtualDesktop
            && activity == other.activity && windowType == other.windowType;
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
