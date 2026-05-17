// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/QtScreenProvider.h"

#include "PhosphorScreens/ScreenIdentity.h"

#include <QGuiApplication>
#include <QScreen>

namespace Phosphor::Screens {

namespace {
/// Snapshot a live QScreen into a PhysicalScreen, resolving the stable
/// EDID-aware identifier. A null QScreen yields the invalid value.
PhysicalScreen toPhysicalScreen(QScreen* screen)
{
    if (!screen) {
        return {};
    }
    return PhysicalScreen{screen->name(), ScreenIdentity::identifierFor(screen), screen->geometry(), screen};
}
} // namespace

QtScreenProvider::QtScreenProvider(QObject* parent)
    : IScreenProvider(parent)
{
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
    watchScreen(screen);
    Q_EMIT screenAdded(toPhysicalScreen(screen));
}

void QtScreenProvider::onQtScreenRemoved(QScreen* screen)
{
    // QGuiApplication emits screenRemoved while the QScreen is still
    // valid, so snapshot it before the geometryChanged disconnect and
    // the signal. The geometryChanged connection would auto-tear with
    // the QScreen anyway; dropping it here just keeps teardown explicit.
    const PhysicalScreen removed = toPhysicalScreen(screen);
    if (screen) {
        disconnect(screen, &QScreen::geometryChanged, this, nullptr);
    }
    Q_EMIT screenRemoved(removed);
}

} // namespace Phosphor::Screens
