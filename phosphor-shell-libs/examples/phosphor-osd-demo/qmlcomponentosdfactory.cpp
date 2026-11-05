// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QmlComponentOSDFactory.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

#include <utility>

namespace PhosphorOsdDemo {

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
    // Resolve the delegate type from its module by name (Qt 6.5+ ctor),
    // so no qrc path is hard-coded.
    QQmlComponent component(engine, m_moduleUri, m_typeName);
    if (component.isError()) {
        qWarning() << "QmlComponentOSDFactory: component error for" << m_id << "—" << component.errorString();
        return nullptr;
    }
    QObject* obj = component.create(engine->rootContext());
    if (!obj) {
        // create() can fail at runtime even when isError() was false at
        // load; surface the actual error rather than the wrong-type message.
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
    // The OSD host owns the delegate's lifetime per the IOSDFactory
    // contract: it destroys the delegate when the OSD swaps or hides. A
    // QObject-parented item defaults to CppOwnership, which makes the
    // host's QML destroy() throw "indestructible object" (and then the
    // old delegate leaks, stacking a ghost behind the new one). Hand
    // ownership to the JS engine so the host's destroy() is valid.
    QQmlEngine::setObjectOwnership(item, QQmlEngine::JavaScriptOwnership);
    return item;
}

} // namespace PhosphorOsdDemo
