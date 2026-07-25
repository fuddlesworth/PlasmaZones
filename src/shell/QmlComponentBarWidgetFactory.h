// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QString>
#include <QStringList>
#include <QtCore/qtclasshelpermacros.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QQuickItem;
class QObject;
QT_END_NAMESPACE

namespace PhosphorShellApp {

// IBarWidgetFactory that wraps a delegate type from a QML module,
// resolved by (module URI, type name) so no qrc path is hard-coded.
// Used by every built-in bar widget (Clock, Battery, ...) since their
// createWidget logic is identical, and it is the kind of helper a real
// shell provides for its built-ins; there is no need for each built-in
// to subclass IBarWidgetFactory. Plugin authors can mirror the pattern
// or supply their own QQmlComponent build logic.
//
// Ownership: createWidget keeps the default CppOwnership and parents the
// created item under `parent`, so the bar host's destruction cascade
// (the Slot Repeater destroys the slot, the QObject parent-chain takes
// the widget with it) reclaims it. The IBarWidgetFactory contract
// forbids the JavaScriptOwnership the OSD factory uses, because the bar
// host never calls destroy() on a widget itself; mixing the two would
// create dual-ownership UB.
class QmlComponentBarWidgetFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    QmlComponentBarWidgetFactory(QString id, QString displayName, QString moduleUri, QString typeName,
                                 QStringList capabilities = {});
    ~QmlComponentBarWidgetFactory() override = default;
    Q_DISABLE_COPY_MOVE(QmlComponentBarWidgetFactory)

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QStringList capabilities() const override;

    [[nodiscard]] QQuickItem* createWidget(QQmlEngine* engine, QObject* parent) override;

private:
    QString m_id;
    QString m_displayName;
    QString m_moduleUri;
    QString m_typeName;
    QStringList m_capabilities;
};

} // namespace PhosphorShellApp
