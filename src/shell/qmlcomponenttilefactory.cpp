// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QmlComponentTileFactory.h"

#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

#include <utility>

namespace {
// Categorised, so these diagnostics can be filtered through
// QT_LOGGING_RULES like every other message the process emits. Bare
// qWarning output cannot be turned off or turned up.
// Distinct name from the controller's own lcControlCenter: both live in
// anonymous namespaces, and a unity build merges the two translation
// units, where identical names collide.
Q_LOGGING_CATEGORY(lcControlCenterTiles, "phosphorshell.controlcenter.tiles")
} // namespace

// NOTE: examples/phosphor-control-center-demo/qmlcomponenttilefactory.cpp carries a copy of this class.
// The duplication is deliberate, so the example stands alone and can be
// lifted into another project, but the two have already drifted once.
// A change here almost certainly belongs there too.
namespace PhosphorShellApp {

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
        qCWarning(lcControlCenterTiles) << "QmlComponentTileFactory: null engine for" << m_id;
        return nullptr;
    }
    // Resolve the tile type from its module by name (Qt 6.5+ ctor), so no
    // qrc path is hard-coded.
    QQmlComponent component(engine, m_moduleUri, m_typeName);
    if (component.isError()) {
        qCWarning(lcControlCenterTiles) << "QmlComponentTileFactory: component error for" << m_id << "—"
                                        << component.errorString();
        return nullptr;
    }
    // A component that is neither Ready nor Error is still loading, and
    // creating from it returns null with an EMPTY errorString, so the
    // failure below would log a blank reason. The bar's widget factory
    // carries the same guard.
    if (component.status() != QQmlComponent::Ready) {
        qCWarning(lcControlCenterTiles) << "QmlComponentTileFactory: component not ready for" << m_id << "— status"
                                        << component.status();
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
        qCWarning(lcControlCenterTiles) << "QmlComponentTileFactory: component creation failed for" << m_id << "—"
                                        << component.errorString();
        return nullptr;
    }
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        qCWarning(lcControlCenterTiles) << "QmlComponentTileFactory: component is not a QQuickItem for" << m_id;
        obj->deleteLater();
        return nullptr;
    }
    auto* parentItem = qobject_cast<QQuickItem*>(parent);
    if (!parentItem) {
        // Falling through to a plain QObject parent would hand back an item
        // with no visual parent, which never renders and says nothing about
        // why. Refuse, as the bar's widget factory does.
        // Refused, not merely reported. Returning the item anyway left it
        // with no visual parent, so it never appeared and the host counted
        // it as a materialised tile. The bar factory does the same.
        qCWarning(lcControlCenterTiles) << "QmlComponentTileFactory: parent is not a QQuickItem for" << m_id
                                        << "— refusing rather than returning an item nothing will show";
        delete item;
        return nullptr;
        item->deleteLater();
        return nullptr;
    }
    item->setParentItem(parentItem);
    // ControlCenter owns the tile's lifetime: rebuild() destroys what it
    // built. A QObject-parented item defaults to CppOwnership, which makes
    // the host's QML destroy() throw "indestructible object" and then leak
    // the old tile behind the new one. Hand ownership to the JS engine so
    // the host's destroy() is valid. Same contract the OSD demo documents.
    QQmlEngine::setObjectOwnership(item, QQmlEngine::JavaScriptOwnership);
    return item;
}

} // namespace PhosphorShellApp
