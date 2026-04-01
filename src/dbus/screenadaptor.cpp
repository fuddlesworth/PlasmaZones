// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenadaptor.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/screenmanager.h"
#include "../core/utils.h"
#include "../core/virtualscreen.h"
#include <QGuiApplication>
#include <QScreen>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace PlasmaZones {

ScreenAdaptor::ScreenAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent)
{
    // Connect to screen change signals
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        const QString physId = Utils::screenIdentifier(screen);

        // When virtual screens exist for this physical screen, emit only the
        // virtual screen IDs — not the physical ID — to avoid phantom entries.
        bool hadVirtual = emitForEffectiveScreens(physId, [this](const QString& id) {
            Q_EMIT screenAdded(id);
        });
        if (hadVirtual) {
            Q_EMIT virtualScreensChanged(physId);
        }

        // Cache screen ID now — QScreen may be partially destroyed when
        // geometryChanged fires during screen hot-unplug sequences.
        const QString cachedId = physId;
        connect(screen, &QScreen::geometryChanged, this, [this, screen, cachedId]() {
            handleScreenGeometryChanged(screen, cachedId);
        });

        // Connect screenRemoved with the cached physical ID so we never query
        // a potentially-destroyed QScreen. The filter ensures this lambda only
        // fires for the screen that was added in this invocation.
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this, screen, cachedId](QScreen* removedScreen) {
            if (removedScreen != screen) {
                return;
            }
            emitForEffectiveScreens(cachedId, [this](const QString& id) {
                Q_EMIT screenRemoved(id);
            });
        });
    });

    // Connect existing screens
    for (auto* screen : Utils::allScreens()) {
        // Cache screen ID at connection time to avoid querying a partially-destroyed QScreen.
        const QString cachedId = Utils::screenIdentifier(screen);
        connect(screen, &QScreen::geometryChanged, this, [this, screen, cachedId]() {
            handleScreenGeometryChanged(screen, cachedId);
        });
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this, screen, cachedId](QScreen* removedScreen) {
            if (removedScreen != screen) {
                return;
            }
            emitForEffectiveScreens(cachedId, [this](const QString& id) {
                Q_EMIT screenRemoved(id);
            });
        });
    }
}

void ScreenAdaptor::handleScreenGeometryChanged(QScreen* screen, const QString& physId)
{
    Q_UNUSED(screen)
    emitForEffectiveScreens(physId, [this](const QString& id) {
        Q_EMIT screenGeometryChanged(id);
    });
}

bool ScreenAdaptor::emitForEffectiveScreens(const QString& physId, const std::function<void(const QString&)>& emitFn)
{
    auto* mgr = ScreenManager::instance();
    if (mgr) {
        QStringList vsIds = mgr->virtualScreenIdsFor(physId);
        if (vsIds.size() > 1 || (!vsIds.isEmpty() && vsIds.first() != physId)) {
            for (const QString& vsId : vsIds) {
                emitFn(vsId);
            }
            return true;
        }
    }
    emitFn(physId);
    return false;
}

QStringList ScreenAdaptor::getScreens()
{
    // Return effective screen IDs (virtual screens when configured, physical otherwise)
    // so consumers (settings app, KCM) see virtual screens as first-class entries.
    auto* mgr = ScreenManager::instance();
    if (mgr) {
        QStringList effective = mgr->effectiveScreenIds();
        if (!effective.isEmpty()) {
            return effective;
        }
    }

    // Fallback: no ScreenManager or no screens tracked yet.
    // Note: when ScreenManager is unavailable, returns physical screen IDs only
    // (virtual screen IDs require ScreenManager to be initialized).
    QStringList result;
    for (const auto* screen : Utils::allScreens()) {
        result.append(Utils::screenIdentifier(screen));
    }
    return result;
}

QString ScreenAdaptor::getScreenInfo(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(lcDbus) << "getScreenInfo: empty screen name";
        return QString();
    }

    // For virtual screens, resolve the backing physical screen for metadata
    // (manufacturer, model, refresh rate) and use ScreenManager for geometry
    auto* mgr = ScreenManager::instance();
    const QScreen* screen = Utils::findScreenByIdOrName(screenId);
    if (!screen && mgr) {
        screen = mgr->physicalQScreenFor(screenId);
    }

    if (screen) {
        // Use virtual screen geometry if available, otherwise physical
        QRect geom = screen->geometry();
        if (mgr && VirtualScreenId::isVirtual(screenId)) {
            QRect vsGeom = mgr->screenGeometry(screenId);
            if (vsGeom.isValid()) {
                geom = vsGeom;
            }
        }

        QJsonObject info;
        info[JsonKeys::Name] = screen->name();
        info[JsonKeys::Manufacturer] = screen->manufacturer();
        info[JsonKeys::Model] = screen->model();
        info[JsonKeys::Geometry] = QJsonObject{{JsonKeys::X, geom.x()},
                                               {JsonKeys::Y, geom.y()},
                                               {JsonKeys::Width, geom.width()},
                                               {JsonKeys::Height, geom.height()}};
        info[JsonKeys::PhysicalSize] = QJsonObject{{JsonKeys::Width, screen->physicalSize().width()},
                                                   {JsonKeys::Height, screen->physicalSize().height()}};
        info[JsonKeys::DevicePixelRatio] = screen->devicePixelRatio();
        info[JsonKeys::RefreshRate] = screen->refreshRate();
        info[JsonKeys::Depth] = screen->depth();
        info[JsonKeys::ScreenId] = screenId;
        info[JsonKeys::SerialNumber] = screen->serialNumber();
        if (VirtualScreenId::isVirtual(screenId)) {
            info[JsonKeys::IsVirtualScreen] = true;
            QString physId = VirtualScreenId::extractPhysicalId(screenId);
            info[JsonKeys::PhysicalScreenId] = physId;
            // Include the user-facing display name (e.g. "Left", "Right")
            if (mgr) {
                VirtualScreenConfig config = mgr->virtualScreenConfig(physId);
                int vsIndex = VirtualScreenId::extractIndex(screenId);
                if (vsIndex >= 0 && vsIndex < config.screens.size()) {
                    info[JsonKeys::VirtualDisplayName] = config.screens[vsIndex].displayName;
                }
            }
        }

        return QString::fromUtf8(QJsonDocument(info).toJson());
    }

    qCWarning(lcDbus) << "Screen not found:" << screenId;
    return QString();
}

QString ScreenAdaptor::getPrimaryScreen()
{
    // Prefer KWin-sourced override (from Workspace::outputOrder) over Qt's
    // QGuiApplication::primaryScreen(), which may diverge from KDE Display
    // Settings on some Wayland configurations.
    if (!m_primaryScreenOverride.isEmpty()) {
        QScreen* overrideScreen = Utils::findScreenByName(m_primaryScreenOverride);
        if (overrideScreen) {
            return Utils::screenIdentifier(overrideScreen);
        }
    }
    auto* primary = Utils::primaryScreen();
    return primary ? Utils::screenIdentifier(primary) : QString();
}

void ScreenAdaptor::setPrimaryScreenFromKWin(const QString& connectorName)
{
    m_primaryScreenOverride = connectorName;
    qCInfo(lcDbus) << "Primary screen override set from KWin:" << connectorName;
}

QString ScreenAdaptor::getScreenId(const QString& connectorName)
{
    if (connectorName.isEmpty()) {
        return QString();
    }
    return Utils::screenIdForName(connectorName);
}

QString ScreenAdaptor::getVirtualScreenConfig(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcDbus) << "getVirtualScreenConfig: empty physicalScreenId";
        return QString();
    }
    if (VirtualScreenId::isVirtual(physicalScreenId)) {
        qCWarning(lcDbus) << "getVirtualScreenConfig: expected physical screen ID, got virtual:" << physicalScreenId;
        return QString();
    }

    auto* mgr = ScreenManager::instance();
    if (!mgr) {
        qCWarning(lcDbus) << "getVirtualScreenConfig: no ScreenManager instance";
        return QString();
    }

    VirtualScreenConfig config = mgr->virtualScreenConfig(physicalScreenId);

    QJsonObject root;
    root[JsonKeys::PhysicalScreenId] = physicalScreenId;

    QJsonArray screensArr;
    for (const auto& vs : config.screens) {
        QJsonObject screenObj;
        screenObj[JsonKeys::Id] = vs.id;
        screenObj[JsonKeys::Index] = vs.index;
        screenObj[JsonKeys::DisplayName] = vs.displayName;
        screenObj[JsonKeys::Region] = QJsonObject{{JsonKeys::X, vs.region.x()},
                                                  {JsonKeys::Y, vs.region.y()},
                                                  {JsonKeys::Width, vs.region.width()},
                                                  {JsonKeys::Height, vs.region.height()}};
        screensArr.append(screenObj);
    }
    root[JsonKeys::Screens] = screensArr;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void ScreenAdaptor::setVirtualScreenConfig(const QString& physicalScreenId, const QString& configJson)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: empty physicalScreenId";
        return;
    }
    if (VirtualScreenId::isVirtual(physicalScreenId)) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: expected physical screen ID, got virtual:" << physicalScreenId;
        return;
    }

    auto* mgr = ScreenManager::instance();
    if (!mgr) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: no ScreenManager instance";
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(configJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: invalid JSON:" << parseError.errorString();
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray screensArr = root[JsonKeys::Screens].toArray();

    VirtualScreenConfig config;
    config.physicalScreenId = physicalScreenId;
    QSet<int> seenIndices;

    for (const auto& entry : screensArr) {
        QJsonObject screenObj = entry.toObject();
        QJsonObject regionObj = screenObj[JsonKeys::Region].toObject();

        VirtualScreenDef def;
        def.index = screenObj[JsonKeys::Index].toInt();
        def.id = VirtualScreenId::make(physicalScreenId, def.index);
        def.physicalScreenId = physicalScreenId;
        def.displayName = screenObj[JsonKeys::DisplayName].toString();
        def.region = QRectF(regionObj[JsonKeys::X].toDouble(), regionObj[JsonKeys::Y].toDouble(),
                            regionObj[JsonKeys::Width].toDouble(), regionObj[JsonKeys::Height].toDouble());

        // Validate region coordinates are within [0.0, 1.0] bounds (relative geometry)
        constexpr qreal tolerance = 1e-6;
        const qreal rx = def.region.x();
        const qreal ry = def.region.y();
        const qreal rw = def.region.width();
        const qreal rh = def.region.height();
        constexpr qreal minSize = 0.01;
        if (rx < -tolerance || ry < -tolerance || rw < minSize || rh < minSize || rx + rw > 1.0 + tolerance
            || ry + rh > 1.0 + tolerance) {
            qCWarning(lcDbus) << "setVirtualScreenConfig: dropping virtual screen" << def.index << "for"
                              << physicalScreenId << "- invalid region: x=" << rx << "y=" << ry << "w=" << rw
                              << "h=" << rh << "(all coordinates must be within [0.0, 1.0])";
            continue;
        }

        // Reject duplicate indices — two entries with the same index would
        // generate the same virtual screen ID, silently clobbering one.
        if (seenIndices.contains(def.index)) {
            qCWarning(lcDbus) << "setVirtualScreenConfig: duplicate virtual screen index" << def.index << "for"
                              << physicalScreenId;
            return;
        }
        seenIndices.insert(def.index);

        config.screens.append(def);
    }

    // Basic sanity: must have at least 2 screens (full validation in ScreenManager)
    if (config.screens.size() < 2) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: need at least 2 screens for subdivision";
        return;
    }

    mgr->setVirtualScreenConfig(physicalScreenId, config);
}

QStringList ScreenAdaptor::getPhysicalScreens()
{
    QStringList result;
    auto* mgr = ScreenManager::instance();
    if (!mgr) {
        return result;
    }
    for (const auto* screen : mgr->screens()) {
        result.append(Utils::screenIdentifier(screen));
    }
    return result;
}

QStringList ScreenAdaptor::getEffectiveScreens()
{
    auto* mgr = ScreenManager::instance();
    if (!mgr) {
        return {};
    }
    return mgr->effectiveScreenIds();
}

QString ScreenAdaptor::getEffectiveScreenAt(int x, int y)
{
    auto* mgr = ScreenManager::instance();
    if (!mgr) {
        return QString();
    }
    return mgr->effectiveScreenAt(QPoint(x, y));
}

} // namespace PlasmaZones
