// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtCore/qtclasshelpermacros.h>

namespace PhosphorRegistryDemo {

// Trivial IBarWidgetFactory wrapping a single QML file URL. The
// factory loads the URL into a QQmlComponent on first createWidget
// and reuses it across subsequent calls. Used by both built-in demo
// widgets (Clock, ColorSquare) since their createWidget logic is
// identical.
//
// This wrapper is the kind of helper a real shell would also
// provide for built-in widgets — there's no need for each built-in
// to subclass IBarWidgetFactory directly. Plugin authors can mirror
// the pattern or supply their own QQmlComponent build logic.
class QmlComponentBarWidgetFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    QmlComponentBarWidgetFactory(QString id, QString displayName, QUrl qmlUrl, QStringList capabilities = {});
    ~QmlComponentBarWidgetFactory() override = default;
    Q_DISABLE_COPY_MOVE(QmlComponentBarWidgetFactory)

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QStringList capabilities() const override;

    [[nodiscard]] QQuickItem* createWidget(QQmlEngine* engine, QObject* parent) override;

private:
    QString m_id;
    QString m_displayName;
    QUrl m_qmlUrl;
    QStringList m_capabilities;
};

} // namespace PhosphorRegistryDemo
