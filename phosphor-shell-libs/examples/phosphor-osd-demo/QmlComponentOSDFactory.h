// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IOSDFactory.h>

#include <QString>
#include <QStringList>
#include <QtCore/qtclasshelpermacros.h>

QT_BEGIN_NAMESPACE
class QObject;
class QQmlEngine;
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorOsdDemo {

// IOSDFactory wrapping one OSD delegate type from a QML module. createOSD
// builds the type via QQmlComponent(engine, uri, typeName), so the
// factory references the registered module type by name rather than a
// fragile qrc path. The kind of helper a real shell would also provide
// for its built-in OSDs; plugin authors mirror the pattern or supply
// their own create logic.
class QmlComponentOSDFactory : public PhosphorRegistry::IOSDFactory
{
public:
    QmlComponentOSDFactory(QString id, QString displayName, QString moduleUri, QString typeName,
                           QStringList capabilities = {});
    ~QmlComponentOSDFactory() override = default;
    Q_DISABLE_COPY_MOVE(QmlComponentOSDFactory)

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QStringList capabilities() const override;

    [[nodiscard]] QQuickItem* createOSD(QQmlEngine* engine, QObject* parent) override;

private:
    QString m_id;
    QString m_displayName;
    QString m_moduleUri;
    QString m_typeName;
    QStringList m_capabilities;
};

} // namespace PhosphorOsdDemo
