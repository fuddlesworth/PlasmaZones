// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IControlCenterTileFactory.h>

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtCore/qtclasshelpermacros.h>

QT_BEGIN_NAMESPACE
class QObject;
class QQmlEngine;
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorControlCenterDemo {

// IControlCenterTileFactory wrapping one tile type from a QML module.
// createTile builds the type via QQmlComponent(engine, uri, typeName), so
// the factory names the registered module type rather than a fragile qrc
// path. Mirrors the OSD demo's QmlComponentOSDFactory; the kind of helper
// a real shell provides for its built-in tiles, and the pattern a plugin
// author copies.
//
// `initialProperties` covers the one thing OSD delegates never needed: a
// tile can have a `required property` that only the host can satisfy
// (IdleTile takes the shell's IdleService, because a tile-owned one would
// arm a second, independent idle ladder). A required property must be set
// at construction, so it goes through createWithInitialProperties rather
// than being assigned afterwards.
class QmlComponentTileFactory : public PhosphorRegistry::IControlCenterTileFactory
{
public:
    QmlComponentTileFactory(QString id, QString displayName, QString moduleUri, QString typeName,
                            QVariantMap initialProperties = {}, QStringList capabilities = {});
    ~QmlComponentTileFactory() override = default;
    Q_DISABLE_COPY_MOVE(QmlComponentTileFactory)

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QStringList capabilities() const override;

    [[nodiscard]] QQuickItem* createTile(QQmlEngine* engine, QObject* parent) override;

private:
    QString m_id;
    QString m_displayName;
    QString m_moduleUri;
    QString m_typeName;
    QVariantMap m_initialProperties;
    QStringList m_capabilities;
};

} // namespace PhosphorControlCenterDemo
