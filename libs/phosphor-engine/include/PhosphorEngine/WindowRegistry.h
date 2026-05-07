// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/IWindowRegistry.h>
#include <phosphorengine_export.h>
#include <QHash>
#include <QMultiHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace PhosphorEngine {

struct WindowMetadata
{
    QString appId;
    QString desktopFile;
    QString title;

    bool operator==(const WindowMetadata& other) const
    {
        return appId == other.appId && desktopFile == other.desktopFile && title == other.title;
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
