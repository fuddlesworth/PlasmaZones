// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QmlComponentBarWidgetFactory.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

namespace PhosphorRegistryDemo {

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
        qWarning() << "QmlComponentBarWidgetFactory: null engine for" << m_id;
        return nullptr;
    }
    // Build the component fresh on each call. Cheap for QML loaded
    // from a qrc-baked resource; a real shell would cache. Keeping
    // it simple here keeps the demo's behaviour obvious.
    QQmlComponent component(engine, m_qmlUrl);
    if (component.isError()) {
        qWarning() << "QmlComponentBarWidgetFactory: component error for" << m_id << "—" << component.errorString();
        return nullptr;
    }
    QObject* obj = component.create(engine->rootContext());
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        qWarning() << "QmlComponentBarWidgetFactory: component is not a QQuickItem for" << m_id;
        if (obj) {
            obj->deleteLater();
        }
        return nullptr;
    }
    item->setParent(parent);
    auto* parentItem = qobject_cast<QQuickItem*>(parent);
    if (parentItem) {
        item->setParentItem(parentItem);
    } else {
        // Without a QQuickItem parent, the widget has no scene-graph
        // hookup — it is QObject-parented (so it won't leak) but
        // will never render. Surfaces always pass a QQuickItem in
        // practice; an !parentItem path here is a wiring bug worth
        // surfacing rather than letting the bar silently miss a
        // widget.
        qWarning() << "QmlComponentBarWidgetFactory: parent is not a QQuickItem for" << m_id
                   << "— widget will be invisible";
    }
    // Default ownership (CppOwnership) is the right model here: the
    // widget is C++-owned via the QObject parent we just set, and
    // its destruction cascades when the parent dies. Mixing in
    // JavaScriptOwnership would create dual-ownership UB (the GC
    // and the parent both believe they're authoritative). Plugin
    // authors who need QML-destroyable widgets must do that opt-in
    // explicitly inside their own factory.
    return item;
}

} // namespace PhosphorRegistryDemo
