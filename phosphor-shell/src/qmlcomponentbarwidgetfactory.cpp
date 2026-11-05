// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QmlComponentBarWidgetFactory.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

#include <utility>

Q_LOGGING_CATEGORY(lcBar, "phosphorshell.bar")

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
        qCWarning(lcBar) << "QmlComponentBarWidgetFactory: null engine for" << m_id;
        return nullptr;
    }
    if (!parent) {
        // With no parent the item would have neither a QObject owner nor a
        // visual one, and CppOwnership means nothing would ever delete it.
        // Refuse before constructing rather than leaking an invisible item.
        qCWarning(lcBar) << "QmlComponentBarWidgetFactory: null parent for" << m_id;
        return nullptr;
    }
    // Resolve the delegate type from its module by name (Qt 6.5+ ctor),
    // so no qrc path is hard-coded.
    QQmlComponent component(engine, m_moduleUri, m_typeName);
    if (component.isError()) {
        qCWarning(lcBar) << "QmlComponentBarWidgetFactory: component error for" << m_id << "—"
                         << component.errorString();
        return nullptr;
    }
    if (!component.isReady()) {
        // The (engine, uri, typeName) ctor resolves a statically linked
        // module synchronously, so this should be unreachable; logging the
        // status distinctly keeps a future async path from surfacing as a
        // creation failure with an empty errorString.
        qCWarning(lcBar) << "QmlComponentBarWidgetFactory: component not ready for" << m_id << "— status"
                         << component.status() << component.errorString();
        return nullptr;
    }
    QObject* obj = component.create(engine->rootContext());
    if (!obj) {
        // create() can fail at runtime even when isError() was false at
        // load; surface the actual error rather than the wrong-type message.
        qCWarning(lcBar) << "QmlComponentBarWidgetFactory: component creation failed for" << m_id << "—"
                         << component.errorString();
        return nullptr;
    }
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        qCWarning(lcBar) << "QmlComponentBarWidgetFactory: component is not a QQuickItem for" << m_id;
        obj->deleteLater();
        return nullptr;
    }
    item->setParent(parent);
    if (auto* parentItem = qobject_cast<QQuickItem*>(parent)) {
        item->setParentItem(parentItem);
    } else {
        // A non-null parent that is not a QQuickItem still owns the widget
        // (so it won't leak), but gives it no scene-graph hookup, so it
        // never renders. Slots always pass a QQuickItem in practice, so
        // this is a wiring bug worth surfacing rather than letting the bar
        // silently miss a widget.
        qCWarning(lcBar) << "QmlComponentBarWidgetFactory: parent is not a QQuickItem for" << m_id
                         << "— widget will be invisible";
    }
    // Pin CppOwnership explicitly rather than leaning on the engine's
    // heuristic. The item is returned through a Q_INVOKABLE, and the rule
    // for that path turns on whether the object already has a parent, which
    // it does here only because setParent ran a few lines up. Stating the
    // invariant outright means a future edit that reorders or drops the
    // parenting cannot silently hand the widget to the JS collector, and it
    // matches how the rest of the tree pins ownership (phosphor-layer's
    // surface, the PipeWire registration). The bar host never calls
    // destroy(), so JS ownership would be dual-ownership UB.
    QQmlEngine::setObjectOwnership(item, QQmlEngine::CppOwnership);
    return item;
}

} // namespace PhosphorShellApp
