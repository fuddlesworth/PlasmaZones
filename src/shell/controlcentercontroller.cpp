// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ControlCenterController.h"

#include "QmlComponentTileFactory.h"

#include <PhosphorServiceIdle/IdleService.h>

#include <QDebug>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QStringLiteral>
#include <QVariant>
#include <QVariantMap>

#include <memory>

namespace PhosphorShellApp {

namespace {
// Every built-in tile lives in the Phosphor.ControlCenter module, under
// the Tiles/ directory. qt_add_qml_module registers each QML file as a
// type named after its basename, so the directory does not appear here.
const QString kModule = QStringLiteral("Phosphor.ControlCenter");
}

ControlCenterController::ControlCenterController(PhosphorServiceIdle::IdleService* idleService, QObject* parent)
    : QObject(parent)
{
    // Capabilities are advisory until the Phase 5 capability runtime lands,
    // but they are declared now so the built-ins exercise the same manifest
    // surface a plugin will.
    const auto reg = [this](const QString& id, const QString& name, const QString& type, const QString& capability,
                            const QVariantMap& initialProperties = {}) {
        m_registry.registerFactory(std::make_shared<QmlComponentTileFactory>(id, name, kModule, type, initialProperties,
                                                                             QStringList{capability}));
        m_tileIds.append(id);
    };

    // Order here is the order they appear in the grid. The two sliders sit
    // last because they span the full width, so the four half-width
    // toggles pack cleanly above them.
    reg(QStringLiteral("network"), QStringLiteral("Wi-Fi"), QStringLiteral("NetworkTile"),
        QStringLiteral("network.write"));
    reg(QStringLiteral("bluetooth"), QStringLiteral("Bluetooth"), QStringLiteral("BluetoothTile"),
        QStringLiteral("bluetooth.write"));
    reg(QStringLiteral("idle"), QStringLiteral("Keep awake"), QStringLiteral("IdleTile"),
        QStringLiteral("idle.inhibit"), QVariantMap{{QStringLiteral("service"), QVariant::fromValue(idleService)}});
    reg(QStringLiteral("audio"), QStringLiteral("Volume"), QStringLiteral("AudioTile"), QStringLiteral("audio.write"));
    reg(QStringLiteral("brightness"), QStringLiteral("Brightness"), QStringLiteral("BrightnessTile"),
        QStringLiteral("brightness.write"));
}

ControlCenterController::~ControlCenterController() = default;

QStringList ControlCenterController::tileIds() const
{
    return m_tileIds;
}

QString ControlCenterController::openScreen() const
{
    return m_openScreen;
}

void ControlCenterController::toggleOnScreen(const QString& screenName)
{
    // Same screen → close. Different screen → move (the assignment below
    // does both legs, since the new value replaces the old). Empty → close.
    const QString next = (screenName.isEmpty() || screenName == m_openScreen) ? QString() : screenName;
    if (next == m_openScreen) {
        return;
    }
    m_openScreen = next;
    Q_EMIT openScreenChanged();
}

void ControlCenterController::close()
{
    if (m_openScreen.isEmpty()) {
        return;
    }
    m_openScreen.clear();
    Q_EMIT openScreenChanged();
}

QString ControlCenterController::screenNameOf(QQuickItem* item) const
{
    if (item) {
        if (const QQuickWindow* window = item->window()) {
            if (const QScreen* screen = window->screen()) {
                return screen->name();
            }
        }
    }
    // An unresolved source should open the panel somewhere sensible rather
    // than nowhere: an empty name matches no bar, so the control center
    // would silently fail to appear.
    if (const QScreen* primary = QGuiApplication::primaryScreen()) {
        return primary->name();
    }
    return {};
}

QQuickItem* ControlCenterController::createTile(const QString& id, QQuickItem* parent)
{
    const auto factory = m_registry.factory(id);
    if (!factory) {
        qWarning() << "ControlCenterController: no tile registered for id" << id;
        return nullptr;
    }
    QQmlEngine* engine = parent ? qmlEngine(parent) : nullptr;
    if (!engine) {
        qWarning() << "ControlCenterController: no QML engine resolvable from parent for id" << id;
        return nullptr;
    }
    return factory->createTile(engine, parent);
}

} // namespace PhosphorShellApp
