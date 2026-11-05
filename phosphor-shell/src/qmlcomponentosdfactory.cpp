// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QmlComponentOSDFactory.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

#include <utility>

namespace PhosphorShellApp {

QmlComponentOSDFactory::QmlComponentOSDFactory(QString id, QString displayName, QString moduleUri, QString typeName,
                                               QStringList capabilities)
    : m_id(std::move(id))
    , m_displayName(std::move(displayName))
    , m_moduleUri(std::move(moduleUri))
    , m_typeName(std::move(typeName))
    , m_capabilities(std::move(capabilities))
{
}

QString QmlComponentOSDFactory::id() const
{
    return m_id;
}

QString QmlComponentOSDFactory::displayName() const
{
    return m_displayName;
}

QStringList QmlComponentOSDFactory::capabilities() const
{
    return m_capabilities;
}

QQuickItem* QmlComponentOSDFactory::createOSD(QQmlEngine* engine, QObject* parent)
{
    if (!engine) {
        qWarning() << "QmlComponentOSDFactory: null engine for" << m_id;
        return nullptr;
    }
    if (!parent) {
        // Without a parent nothing would own the item until the host
        // adopts it, and a failed adoption would leak it.
        qWarning() << "QmlComponentOSDFactory: null parent for" << m_id;
        return nullptr;
    }
    // Resolve the delegate type from its module by name, so no qrc path is
    // hard-coded.
    QQmlComponent component(engine, m_moduleUri, m_typeName);
    if (component.isError()) {
        qWarning() << "QmlComponentOSDFactory: component error for" << m_id << "—" << component.errorString();
        return nullptr;
    }
    QObject* obj = component.create(engine->rootContext());
    if (!obj) {
        qWarning() << "QmlComponentOSDFactory: component creation failed for" << m_id << "—" << component.errorString();
        return nullptr;
    }
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        qWarning() << "QmlComponentOSDFactory: component is not a QQuickItem for" << m_id;
        obj->deleteLater();
        return nullptr;
    }
    if (auto* parentItem = qobject_cast<QQuickItem*>(parent)) {
        item->setParentItem(parentItem);
    } else {
        item->setParent(parent);
    }
    // The host owns the delegate per the IOSDFactory contract and destroys
    // it from QML when the band swaps or hides. A QObject-parented item
    // defaults to CppOwnership, which makes that destroy() throw
    // "indestructible object" and leak the old band behind the new one.
    QQmlEngine::setObjectOwnership(item, QQmlEngine::JavaScriptOwnership);
    return item;
}

} // namespace PhosphorShellApp
