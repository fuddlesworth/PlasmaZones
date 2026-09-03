// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QmlComponentTileFactory.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

#include <utility>

namespace PhosphorControlCenterDemo {

QmlComponentTileFactory::QmlComponentTileFactory(QString id, QString displayName, QString moduleUri, QString typeName,
                                                 QVariantMap initialProperties, QStringList capabilities)
    : m_id(std::move(id))
    , m_displayName(std::move(displayName))
    , m_moduleUri(std::move(moduleUri))
    , m_typeName(std::move(typeName))
    , m_initialProperties(std::move(initialProperties))
    , m_capabilities(std::move(capabilities))
{
}

QString QmlComponentTileFactory::id() const
{
    return m_id;
}

QString QmlComponentTileFactory::displayName() const
{
    return m_displayName;
}

QStringList QmlComponentTileFactory::capabilities() const
{
    return m_capabilities;
}

QQuickItem* QmlComponentTileFactory::createTile(QQmlEngine* engine, QObject* parent)
{
    if (!engine) {
        qWarning() << "QmlComponentTileFactory: null engine for" << m_id;
        return nullptr;
    }
    // Resolve the tile type from its module by name (Qt 6.5+ ctor), so no
    // qrc path is hard-coded.
    QQmlComponent component(engine, m_moduleUri, m_typeName);
    if (component.isError()) {
        qWarning() << "QmlComponentTileFactory: component error for" << m_id << "—" << component.errorString();
        return nullptr;
    }
    // createWithInitialProperties, not create() + assignment: a tile may
    // declare a `required property` the host has to satisfy, and a required
    // property is only settable at construction. Assigning afterwards
    // fails the object outright.
    QObject* obj = component.createWithInitialProperties(m_initialProperties, engine->rootContext());
    if (!obj) {
        // create can fail at runtime even when isError() was false at
        // load; surface the actual error rather than the wrong-type message.
        qWarning() << "QmlComponentTileFactory: component creation failed for" << m_id << "—"
                   << component.errorString();
        return nullptr;
    }
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        qWarning() << "QmlComponentTileFactory: component is not a QQuickItem for" << m_id;
        obj->deleteLater();
        return nullptr;
    }
    if (auto* parentItem = qobject_cast<QQuickItem*>(parent)) {
        item->setParentItem(parentItem);
    } else {
        item->setParent(parent);
    }
    // ControlCenter owns the tile's lifetime: rebuild() destroys what it
    // built. A QObject-parented item defaults to CppOwnership, which makes
    // the host's QML destroy() throw "indestructible object" and then leak
    // the old tile behind the new one. Hand ownership to the JS engine so
    // the host's destroy() is valid. Same contract the OSD demo documents.
    QQmlEngine::setObjectOwnership(item, QQmlEngine::JavaScriptOwnership);
    return item;
}

} // namespace PhosphorControlCenterDemo
