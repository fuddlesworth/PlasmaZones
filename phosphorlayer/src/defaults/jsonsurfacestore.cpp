// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/JsonSurfaceStore.h>

#include "../internal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace PhosphorLayer {

class JsonSurfaceStore::Impl
{
public:
    explicit Impl(QString path)
        : m_path(std::move(path))
    {
        loadFromDisk();
    }

    void loadFromDisk()
    {
        QFile file(m_path);
        if (!file.exists()) {
            m_root = {};
            return;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: cannot read" << m_path << ":" << file.errorString();
            m_root = {};
            return;
        }
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: parse error in" << m_path << ":" << err.errorString();
            m_root = {};
            return;
        }
        m_root = doc.object();
    }

    bool flushToDisk()
    {
        QFileInfo(m_path).dir().mkpath(QStringLiteral("."));
        QSaveFile file(m_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: cannot write" << m_path << ":" << file.errorString();
            return false;
        }
        const auto data = QJsonDocument(m_root).toJson(QJsonDocument::Indented);
        if (file.write(data) != data.size()) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: short write to" << m_path;
            return false;
        }
        if (!file.commit()) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: commit failed for" << m_path << ":" << file.errorString();
            return false;
        }
        return true;
    }

    QString m_path;
    QJsonObject m_root;
};

JsonSurfaceStore::JsonSurfaceStore(QString filePath)
    : m_impl(std::make_unique<Impl>(std::move(filePath)))
{
}

JsonSurfaceStore::~JsonSurfaceStore() = default;

QString JsonSurfaceStore::filePath() const
{
    return m_impl->m_path;
}

bool JsonSurfaceStore::save(const QString& key, const QJsonObject& data)
{
    m_impl->m_root.insert(key, data);
    return m_impl->flushToDisk();
}

QJsonObject JsonSurfaceStore::load(const QString& key) const
{
    const auto v = m_impl->m_root.value(key);
    return v.isObject() ? v.toObject() : QJsonObject();
}

bool JsonSurfaceStore::has(const QString& key) const
{
    return m_impl->m_root.contains(key);
}

void JsonSurfaceStore::remove(const QString& key)
{
    if (!m_impl->m_root.contains(key)) {
        return;
    }
    m_impl->m_root.remove(key);
    m_impl->flushToDisk();
}

} // namespace PhosphorLayer
