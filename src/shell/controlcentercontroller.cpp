// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ControlCenterController.h"

#include "QmlComponentTileFactory.h"

#include <PhosphorServiceIdle/IdleService.h>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QStringLiteral>
#include <QVariant>
#include <QVariantMap>

#include <memory>

namespace {
// Categorised so the control center's diagnostics can be filtered through
// QT_LOGGING_RULES like every sibling in the shell process. The bar
// controller and its widget factory use lcBar for the same messages.
Q_LOGGING_CATEGORY(lcControlCenter, "phosphorshell.controlcenter")
} // namespace

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
        // Only advertise what actually registered. A duplicate id is refused
        // by the registry, and appending regardless would list it twice in
        // tileIds while createTile resolved both entries to the first
        // factory, rendering the same tile twice.
        if (!m_registry.registerFactory(std::make_shared<QmlComponentTileFactory>(
                id, name, kModule, type, initialProperties, QStringList{capability}))) {
            qCWarning(lcControlCenter) << "duplicate control-center tile id refused:" << id;
            return;
        }
        m_tileIds.append(id);
    };

    // Order here is the order they appear in the grid. The two sliders sit
    // last because they span the full width, so the three half-width
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

void ControlCenterController::setOpenScreen(const QString& screenName)
{
    if (m_openScreen == screenName) {
        return;
    }
    m_openScreen = screenName;
    Q_EMIT openScreenChanged();
}

QScreen* ControlCenterController::screenOf(QQuickItem* item) const
{
    QScreen* screen = nullptr;
    if (item) {
        if (const QQuickWindow* window = item->window()) {
            screen = window->screen();
        }
    }
    // An unresolved source should open the panel somewhere sensible rather
    // than nowhere: a null targetScreen would leave the socket transport
    // with no bar to name.
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    // LOAD-BEARING. A QScreen has no QObject parent, and QML's rule for a
    // Q_INVOKABLE returning a parentless QObject* is JavaScriptOwnership:
    // the JS garbage collector DELETES the object once its wrapper is
    // collected. Without this line the GC destroyed the live QScreen — on
    // the next engine teardown at hot reload (ScreenModel then dereferenced
    // a freed screen in PerScreenPanels::build, cores 531950 / 543377 /
    // 553625 / 554585 on 2026-09-03), and, with different GC timing, in the
    // middle of the very next IPC toggle (cores 499902 / 522648). The screen
    // belongs to QGuiApplication; say so.
    if (screen) {
        QQmlEngine::setObjectOwnership(screen, QQmlEngine::CppOwnership);
    }
    return screen;
}

QQuickItem* ControlCenterController::createTile(const QString& id, QQuickItem* parent)
{
    const auto factory = m_registry.factory(id);
    if (!factory) {
        qCWarning(lcControlCenter) << "no tile registered for id" << id;
        return nullptr;
    }
    QQmlEngine* engine = parent ? qmlEngine(parent) : nullptr;
    if (!engine) {
        qCWarning(lcControlCenter) << "no QML engine resolvable from parent for id" << id;
        return nullptr;
    }
    return factory->createTile(engine, parent);
}

} // namespace PhosphorShellApp
