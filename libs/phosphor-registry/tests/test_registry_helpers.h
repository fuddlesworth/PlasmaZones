// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QStringList>
#include <QtCore/qtclasshelpermacros.h>

// FakeBarWidgetFactory — concrete IBarWidgetFactory for unit tests.
// Returns a fresh QQuickItem on createWidget; lets tests assert
// register / unregister / lookup without dragging in real widget
// implementations.
class FakeBarWidgetFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    FakeBarWidgetFactory(QString id, QString displayName, QStringList capabilities = {})
        : m_id(std::move(id))
        , m_displayName(std::move(displayName))
        , m_capabilities(std::move(capabilities))
    {
    }
    ~FakeBarWidgetFactory() override = default;
    Q_DISABLE_COPY_MOVE(FakeBarWidgetFactory)

    [[nodiscard]] QString id() const override
    {
        return m_id;
    }
    [[nodiscard]] QString displayName() const override
    {
        return m_displayName;
    }
    [[nodiscard]] QStringList capabilities() const override
    {
        return m_capabilities;
    }

    [[nodiscard]] QQuickItem* createWidget(QQmlEngine* /*engine*/, QObject* parent) override
    {
        ++createCallCount;
        auto* item = new QQuickItem(qobject_cast<QQuickItem*>(parent));
        if (!parent || !qobject_cast<QQuickItem*>(parent)) {
            item->setParent(parent);
        }
        return item;
    }

    int createCallCount = 0;

private:
    QString m_id;
    QString m_displayName;
    QStringList m_capabilities;
};
