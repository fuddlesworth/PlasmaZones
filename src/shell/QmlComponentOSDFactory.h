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

namespace PhosphorShellApp {

// IOSDFactory wrapping one OSD delegate type from a QML module, the
// shell's counterpart of QmlComponentBarWidgetFactory. createOSD builds
// the type via QQmlComponent(engine, uri, typeName), so the factory names
// the registered module type rather than a qrc path. The demo under
// examples/phosphor-osd-demo keeps its own copy on purpose: an example
// must not depend on the shell binary's sources.
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

    // Null on a null engine or parent, on a component error, or when the
    // type is not an Item. The returned item is JS-owned: OSDHost destroys
    // it when the band swaps or hides.
    [[nodiscard]] QQuickItem* createOSD(QQmlEngine* engine, QObject* parent) override;

private:
    QString m_id;
    QString m_displayName;
    QString m_moduleUri;
    QString m_typeName;
    QStringList m_capabilities;
};

} // namespace PhosphorShellApp
