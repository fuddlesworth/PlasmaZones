// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/phosphortheme_export.h>
#include <QObject>
#include <QVariantMap>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

namespace PhosphorTheme {
class PHOSPHORTHEME_EXPORT AppearanceStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit AppearanceStore(QObject* parent = nullptr);
    explicit AppearanceStore(const QString& path, QObject* parent = nullptr);
    QVariantMap values() const
    {
        return m_values;
    }
    QString error() const
    {
        return m_error;
    }
    static QVariantMap defaults();
    Q_INVOKABLE bool setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE bool moveWidget(const QString& id, const QString& region, int index = -1);
    Q_INVOKABLE bool resetBarLayout();
    Q_INVOKABLE bool applyPreset(const QString& preset);
    Q_INVOKABLE bool importPreset(const QUrl& file);
    Q_INVOKABLE bool exportPreset(const QUrl& file);
Q_SIGNALS:
    void changed();
    void geometryChanged();
    void errorChanged();

private:
    static bool validate(const QVariantMap& values, QVariantMap& result);
    bool commit(const QVariantMap& values);
    bool fail(const QString& error);
    QString m_path;
    QString m_error;
    QVariantMap m_values;
};
}
