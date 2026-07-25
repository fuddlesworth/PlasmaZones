// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QmlComponentBarWidgetFactory.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

#include <utility>

namespace PhosphorShellApp {

QmlComponentBarWidgetFactory::QmlComponentBarWidgetFactory(QString id, QString displayName, QString moduleUri,
                                                           QString typeName, QStringList capabilities)
    : m_id(std::move(id))
    , m_displayName(std::move(displayName))
    , m_moduleUri(std::move(moduleUri))
    , m_typeName(std::move(typeName))
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
        qWarning() << "QmlComponentBarWidgetFactory: null engine for" << m_id;
        return nullptr;
    }
    // Resolve the delegate type from its module by name (Qt 6.5+ ctor),
    // so no qrc path is hard-coded.
    QQmlComponent component(engine, m_moduleUri, m_typeName);
    if (component.isError()) {
        qWarning() << "QmlComponentBarWidgetFactory: component error for" << m_id << "—" << component.errorString();
        return nullptr;
    }
    QObject* obj = component.create(engine->rootContext());
    if (!obj) {
        // create() can fail at runtime even when isError() was false at
        // load; surface the actual error rather than the wrong-type message.
        qWarning() << "QmlComponentBarWidgetFactory: component creation failed for" << m_id << "—"
                   << component.errorString();
        return nullptr;
    }
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        qWarning() << "QmlComponentBarWidgetFactory: component is not a QQuickItem for" << m_id;
        obj->deleteLater();
        return nullptr;
    }
    item->setParent(parent);
    if (auto* parentItem = qobject_cast<QQuickItem*>(parent)) {
        item->setParentItem(parentItem);
    } else {
        // Without a QQuickItem parent the widget has no scene-graph
        // hookup: it is QObject-parented (so it won't leak) but never
        // renders. Slots always pass a QQuickItem in practice, so an
        // !parentItem path here is a wiring bug worth surfacing rather
        // than letting the bar silently miss a widget.
        qWarning() << "QmlComponentBarWidgetFactory: parent is not a QQuickItem for" << m_id
                   << "— widget will be invisible";
    }
    // Keep the default CppOwnership: the widget is owned via the QObject
    // parent set above and its destruction cascades when the slot dies.
    // Handing it to the JS GC (as the OSD factory does) would create
    // dual-ownership UB here, since the bar host never calls destroy()
    // on a widget itself.
    return item;
}

} // namespace PhosphorShellApp
