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

// One built-in tile, as named fields rather than a run of positional
// QStrings. The display name and the QML type name are both plain strings
// with no validation between them, so a positional call let a rename swap
// the two: the registry would then carry a human label where the type
// belongs, every tile would fail to construct at runtime, and a test that
// only checks the id order would stay green. Designated initialisers make
// that swap a rename rather than a reorder.
struct TileSpec
{
    QString id;
    QString displayName;
    QString typeName;
    QString capability;
    QVariantMap initialProperties;
};
} // namespace

ControlCenterController::ControlCenterController(PhosphorServiceIdle::IdleService* idleService, QObject* parent)
    : QObject(parent)
{
    // The display names are UNTRANSLATED, deliberately and for the same
    // reason BarController's are: the factory contract leaves translation to
    // the implementation, no surface renders these yet (the tile's own label
    // comes from its QML, which does translate), and the shell tier has no
    // i18n wiring on either side. They become PhosphorI18n::tr() calls
    // together with the rest of the shell's strings; translating these alone
    // would localise a label nothing displays and split the two controllers.
    //
    // Capabilities are advisory until the Phase 5 capability runtime lands,
    // but they are declared now so the built-ins exercise the same manifest
    // surface a plugin will.
    const auto reg = [this](const TileSpec& spec) {
        // Only advertise what actually registered. A duplicate id is refused
        // by the registry, and appending regardless would list it twice in
        // tileIds while createTile resolved both entries to the first
        // factory, rendering the same tile twice.
        if (!m_registry.registerFactory(std::make_shared<QmlComponentTileFactory>(spec.id, spec.displayName, kModule,
                                                                                  spec.typeName, spec.initialProperties,
                                                                                  QStringList{spec.capability}))) {
            qCWarning(lcControlCenter) << "duplicate control-center tile id refused:" << spec.id;
            return;
        }
        m_tileIds.append(spec.id);
    };

    // Order here is the order they appear in the grid. The two sliders sit
    // last because they span the full width, so the three half-width
    // toggles (network, bluetooth, idle) pack cleanly above them.
    reg({.id = QStringLiteral("network"),
         .displayName = QStringLiteral("Wi-Fi"),
         .typeName = QStringLiteral("NetworkTile"),
         .capability = QStringLiteral("network.write"),
         // Spelled out because GCC warns on a designated initialiser that
         // skips a field, even one with a default.
         .initialProperties = {}});
    reg({.id = QStringLiteral("bluetooth"),
         .displayName = QStringLiteral("Bluetooth"),
         .typeName = QStringLiteral("BluetoothTile"),
         .capability = QStringLiteral("bluetooth.write"),
         // Spelled out because GCC warns on a designated initialiser that
         // skips a field, even one with a default.
         .initialProperties = {}});
    reg({.id = QStringLiteral("idle"),
         .displayName = QStringLiteral("Keep awake"),
         .typeName = QStringLiteral("IdleTile"),
         .capability = QStringLiteral("idle.inhibit"),
         .initialProperties = QVariantMap{{QStringLiteral("service"), QVariant::fromValue(idleService)}}});
    reg({.id = QStringLiteral("audio"),
         .displayName = QStringLiteral("Volume"),
         .typeName = QStringLiteral("AudioTile"),
         .capability = QStringLiteral("audio.write"),
         // Spelled out because GCC warns on a designated initialiser that
         // skips a field, even one with a default.
         .initialProperties = {}});
    reg({.id = QStringLiteral("brightness"),
         .displayName = QStringLiteral("Brightness"),
         .typeName = QStringLiteral("BrightnessTile"),
         .capability = QStringLiteral("brightness.write"),
         // Spelled out because GCC warns on a designated initialiser that
         // skips a field, even one with a default.
         .initialProperties = {}});
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
