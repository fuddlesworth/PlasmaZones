// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/QtScreenProvider.h"

#include "PhosphorScreens/ScreenIdentity.h"
#include "screenslogging.h"

#include <QGuiApplication>
#include <QScreen>

namespace PhosphorScreens {

namespace {
/// Snapshot a live QScreen into a PhysicalScreen, resolving the stable
/// EDID-aware identifier. A null QScreen yields the invalid value.
PhysicalScreen toPhysicalScreen(QScreen* screen)
{
    if (!screen) {
        return {};
    }
    return PhysicalScreen{.name = screen->name(),
                          .identifier = ScreenIdentity::identifierFor(screen),
                          .geometry = screen->geometry(),
                          .qscreen = screen};
}
} // namespace

QtScreenProvider::QtScreenProvider(QObject* parent)
    : IScreenProvider(parent)
{
    if (!qApp) {
        // Constructed before QGuiApplication exists — Qt's screen signals
        // cannot be wired up, so this provider would stay permanently deaf
        // to add/remove/geometry events. Surface it rather than fail
        // silently; the default-provider path runs in ScreenManager's own
        // constructor, so a misordered host setup lands here.
        qCWarning(lcPhosphorScreens) << "QtScreenProvider constructed before QGuiApplication exists — "
                                        "screen add/remove/geometry events will not be delivered.";
        return;
    }
    connect(qApp, &QGuiApplication::screenAdded, this, &QtScreenProvider::onQtScreenAdded);
    connect(qApp, &QGuiApplication::screenRemoved, this, &QtScreenProvider::onQtScreenRemoved);
    for (auto* screen : QGuiApplication::screens()) {
        watchScreen(screen);
    }
}

QVector<PhysicalScreen> QtScreenProvider::screens() const
{
    QVector<PhysicalScreen> result;
    const auto qtScreens = QGuiApplication::screens();
    result.reserve(qtScreens.size());
    for (auto* screen : qtScreens) {
        result.append(toPhysicalScreen(screen));
    }
    return result;
}

PhysicalScreen QtScreenProvider::primaryScreen() const
{
    return toPhysicalScreen(QGuiApplication::primaryScreen());
}

void QtScreenProvider::watchScreen(QScreen* screen)
{
    if (!screen) {
        return;
    }
    // The lambda captures `screen` raw, but the connection's sender IS
    // `screen` — Qt tears the connection down when the QScreen is
    // destroyed, so the capture can never outlive its referent.
    connect(screen, &QScreen::geometryChanged, this, [this, screen]() {
        Q_EMIT screenGeometryChanged(toPhysicalScreen(screen));
    });
}

void QtScreenProvider::onQtScreenAdded(QScreen* screen)
{
    // A topology change can promote or demote identical-monitor
    // disambiguation suffixes on screens OTHER than this one, so drop the
    // computed-identifier cache before deriving any PhysicalScreen — the
    // ScreenManager's identifier-drift detection depends on screens()
    // returning freshly disambiguated identifiers from here on.
    ScreenIdentity::invalidateComputedIdentifiers();
    watchScreen(screen);
    Q_EMIT screenAdded(toPhysicalScreen(screen));
}

void QtScreenProvider::onQtScreenRemoved(QScreen* screen)
{
    // QGuiApplication emits screenRemoved while the QScreen is still
    // valid, so snapshot it — with its pre-removal identifier — before any
    // cache invalidation or the geometryChanged disconnect. The
    // geometryChanged connection would auto-tear with the QScreen anyway;
    // dropping it here just keeps teardown explicit.
    const PhysicalScreen removed = toPhysicalScreen(screen);
    if (screen) {
        disconnect(screen, &QScreen::geometryChanged, this, nullptr);
        // The EDID-serial cache is keyed on the connector: drop this
        // connector's entry so a different monitor hot-plugged onto the
        // same port resolves fresh.
        ScreenIdentity::invalidateEdidCache(removed.name);
    }
    // Rebuild disambiguation for the surviving screens — removing a
    // same-model sibling can collapse a "/CONNECTOR" suffix back to a
    // bare identifier.
    ScreenIdentity::invalidateComputedIdentifiers();
    Q_EMIT screenRemoved(removed);
}

} // namespace PhosphorScreens
