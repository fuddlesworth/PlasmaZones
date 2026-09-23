// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Copy of the in-process demo's helper. Both demos use the same
// pattern for built-in widgets — wrap a QML file URL in an
// IBarWidgetFactory. Keeping the two copies in sync is acceptable
// for demo code; a real shell would have one helper in a shared
// utility library.

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtCore/qtclasshelpermacros.h>

namespace PhosphorRegistryPluginDemo {

class QmlComponentBarWidgetFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    QmlComponentBarWidgetFactory(QString id, QString displayName, QUrl qmlUrl, QStringList capabilities = {});
    ~QmlComponentBarWidgetFactory() override = default;
    Q_DISABLE_COPY_MOVE(QmlComponentBarWidgetFactory)

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QStringList capabilities() const override;

    [[nodiscard]] QQuickItem* createWidget(QQmlEngine* engine, QObject* parent) override;

private:
    QString m_id;
    QString m_displayName;
    QUrl m_qmlUrl;
    QStringList m_capabilities;
};

} // namespace PhosphorRegistryPluginDemo
