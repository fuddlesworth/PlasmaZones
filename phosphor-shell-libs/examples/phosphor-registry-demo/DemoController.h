// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/Registry.h>

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QStringList>

#include <memory>

namespace PhosphorRegistryDemo {

// QML-exposed glue for the demo. Owns the Registry, registers the
// built-in factories at construction, and exposes a Q_INVOKABLE API
// the bar QML uses to enumerate ids and create widgets on demand.
class DemoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList factoryIds READ factoryIds NOTIFY factoryIdsChanged)
public:
    explicit DemoController(QObject* parent = nullptr);
    ~DemoController() override;
    Q_DISABLE_COPY_MOVE(DemoController)

    // Set the QML engine the controller will hand to factories when
    // they need to compile QML. MUST be called exactly once, from
    // main() AFTER both this controller and the engine are
    // constructed, BEFORE setContextProperty / loadFromModule. This
    // lets the controller be declared on the stack BEFORE the
    // engine — C++ reverse-order destruction then tears the engine
    // down first (clearing every QML binding to the context
    // property) before this controller dies. The QPointer goes
    // null when the engine dies; createWidgetFor's null-check
    // covers the post-teardown re-entry case where QML somehow
    // calls the slot during engine shutdown.
    void setEngine(QQmlEngine* engine);

    // QML calls this from the bar's Component.onCompleted with a
    // host Item it wants child widgets parented under. For each
    // registered factory the bar gets back a QQuickItem the bar
    // QML can use a Repeater-equivalent helper to mount.
    Q_INVOKABLE QQuickItem* createWidgetFor(const QString& id, QQuickItem* parent);

    [[nodiscard]] QStringList factoryIds() const;

Q_SIGNALS:
    void factoryIdsChanged();

private:
    void registerBuiltins();

    QPointer<QQmlEngine> m_engine;
    std::unique_ptr<PhosphorRegistry::Registry<PhosphorRegistry::IBarWidgetFactory>> m_registry;
};

} // namespace PhosphorRegistryDemo
