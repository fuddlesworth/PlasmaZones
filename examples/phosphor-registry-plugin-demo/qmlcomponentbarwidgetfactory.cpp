// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QmlComponentBarWidgetFactory.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

namespace PhosphorRegistryPluginDemo {

QmlComponentBarWidgetFactory::QmlComponentBarWidgetFactory(QString id, QString displayName, QUrl qmlUrl,
                                                           QStringList capabilities)
    : m_id(std::move(id))
    , m_displayName(std::move(displayName))
    , m_qmlUrl(std::move(qmlUrl))
    , m_capabilities(std::move(capabilities))
{
}

QString QmlComponentBarWidgetFactory::id() const
{
    return m_id;
}

QString QmlComponentBarWidgetFactory::displayName() const
{
    return m_displayName;
}

QStringList QmlComponentBarWidgetFactory::capabilities() const
{
    return m_capabilities;
}

QQuickItem* QmlComponentBarWidgetFactory::createWidget(QQmlEngine* engine, QObject* parent)
{
    if (!engine) {
        return nullptr;
    }
    QQmlComponent component(engine, m_qmlUrl);
    if (component.isError()) {
        qWarning() << "QmlComponentBarWidgetFactory: component error for" << m_id << "—" << component.errorString();
        return nullptr;
    }
    QObject* obj = component.create(engine->rootContext());
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        if (obj) {
            obj->deleteLater();
        }
        return nullptr;
    }
    item->setParent(parent);
    if (auto* parentItem = qobject_cast<QQuickItem*>(parent)) {
        item->setParentItem(parentItem);
    }
    QQmlEngine::setObjectOwnership(item, QQmlEngine::JavaScriptOwnership);
    return item;
}

} // namespace PhosphorRegistryPluginDemo
