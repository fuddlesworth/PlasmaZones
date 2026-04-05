// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenadaptor.h"
#include "dbushelpers.h"
#include "../config/configdefaults.h"
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
#include <QTimer>

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
            auto* mgr2 = ScreenManager::instance();
            if (mgr2) {
                m_lastEmittedEffectiveIds = mgr2->effectiveScreenIds();
            }
            Q_EMIT virtualScreensChanged(physId);
        } else {
            // ScreenManager may not be fully initialized yet — defer a re-check
            // so virtual screen signals are emitted once the event loop settles.
            QTimer::singleShot(0, this, [this, physId]() {
                auto* mgr = ScreenManager::instance();
                if (!mgr) {
                    return;
                }
                // Guard against double emission: if effective screen IDs haven't
                // changed since last emission, suppress the duplicate signals.
                QStringList currentEffective = mgr->effectiveScreenIds();
                if (currentEffective == m_lastEmittedEffectiveIds) {
                    return;
                }
                m_lastEmittedEffectiveIds = currentEffective;

                if (mgr->hasVirtualScreens(physId)) {
                    for (const QString& vsId : mgr->virtualScreenIdsFor(physId)) {
                        Q_EMIT screenAdded(vsId);
                    }
                    Q_EMIT virtualScreensChanged(physId);
                }
            });
        }

        // Cache screen ID now — QScreen may be partially destroyed when
        // geometryChanged fires during screen hot-unplug sequences.
        const QString cachedId = physId;
        connect(screen, &QScreen::geometryChanged, this, [this, screen, cachedId]() {
            handleScreenGeometryChanged(screen, cachedId);
        });

        // Cache effective screen IDs in a member map so they can be updated when
        // virtualScreensChanged fires. When screenRemoved fires, ScreenManager may
        // have already cleaned up virtual screen data, so we rely on cached IDs.
        auto* mgrForCache = ScreenManager::instance();
        if (mgrForCache && mgrForCache->hasVirtualScreens(physId)) {
            m_cachedEffectiveIdsPerScreen[physId] = mgrForCache->virtualScreenIdsFor(physId);
        }

        // Use screen as the context object so the connection is automatically
        // destroyed when Qt deletes the QScreen on removal. This prevents dead
        // lambda accumulation on repeated dock/undock cycles.
        connect(qGuiApp, &QGuiApplication::screenRemoved, screen, [this, screen, cachedId](QScreen* removedScreen) {
            if (removedScreen != screen) {
                return;
            }
            const QStringList effectiveIds = m_cachedEffectiveIdsPerScreen.take(cachedId);
            if (!effectiveIds.isEmpty()) {
                for (const QString& id : effectiveIds) {
                    Q_EMIT screenRemoved(id);
                }
            } else {
                Q_EMIT screenRemoved(cachedId);
            }
        });
    });

    // Connect existing screens
    for (auto* screen : Utils::allScreens()) {
        // Cache screen ID at connection time to avoid querying a partially-destroyed QScreen.
        const QString cachedId = Utils::screenIdentifier(screen);
        connect(screen, &QScreen::geometryChanged, this, [this, screen, cachedId]() {
            handleScreenGeometryChanged(screen, cachedId);
        });

        // Cache effective screen IDs in member map (same rationale as screenAdded above).
        auto* mgrForExisting = ScreenManager::instance();
        if (mgrForExisting && mgrForExisting->hasVirtualScreens(cachedId)) {
            m_cachedEffectiveIdsPerScreen[cachedId] = mgrForExisting->virtualScreenIdsFor(cachedId);
        }

        connect(qGuiApp, &QGuiApplication::screenRemoved, screen, [this, screen, cachedId](QScreen* removedScreen) {
            if (removedScreen != screen) {
                return;
            }
            const QStringList effectiveIds = m_cachedEffectiveIdsPerScreen.take(cachedId);
            if (!effectiveIds.isEmpty()) {
                for (const QString& id : effectiveIds) {
                    Q_EMIT screenRemoved(id);
                }
            } else {
                Q_EMIT screenRemoved(cachedId);
            }
        });
    }

    // Update cached effective IDs when virtual screen config changes,
    // so screenRemoved signals emit the current IDs, not stale ones.
    if (auto* mgr = ScreenManager::instance()) {
        connect(mgr, &ScreenManager::virtualScreensChanged, this, [this](const QString& physId) {
            auto* m = ScreenManager::instance();
            if (m && m->hasVirtualScreens(physId)) {
                m_cachedEffectiveIdsPerScreen[physId] = m->virtualScreenIdsFor(physId);
            } else {
                m_cachedEffectiveIdsPerScreen.remove(physId);
            }
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
        bool hadVirtual = mgr->hasVirtualScreens(physId);
        const QStringList ids = mgr->effectiveIdsForPhysical(physId);
        for (const QString& id : ids) {
            emitFn(id);
        }
        return hadVirtual;
    }
    emitFn(physId);
    return false;
}

QStringList ScreenAdaptor::getScreens()
{
    // Return effective screen IDs (virtual screens when configured, physical otherwise)
    // so consumers (settings app, KCM) see virtual screens as first-class entries.
    // Falls back to physical screen IDs when ScreenManager is unavailable.
    return ScreenManager::effectiveScreenIdsWithFallback();
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
    const QScreen* screen = DbusHelpers::resolvePhysicalQScreen(screenId);

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
        // Scale physical size proportionally for virtual screens.
        // A VS covering width=0.5 of the physical monitor gets half the mm width.
        QSizeF physSize = screen->physicalSize();
        const bool isVirtual = VirtualScreenId::isVirtual(screenId);
        const QString physId = isVirtual ? VirtualScreenId::extractPhysicalId(screenId) : QString();
        const int vsIndex = isVirtual ? VirtualScreenId::extractIndex(screenId) : -1;
        const VirtualScreenConfig vsConfig =
            (isVirtual && mgr) ? mgr->virtualScreenConfig(physId) : VirtualScreenConfig();
        if (isVirtual && vsIndex >= 0 && vsIndex < vsConfig.screens.size()) {
            const QRectF& region = vsConfig.screens[vsIndex].region;
            physSize = QSizeF(physSize.width() * region.width(), physSize.height() * region.height());
        }
        info[JsonKeys::PhysicalSize] =
            QJsonObject{{JsonKeys::Width, physSize.width()}, {JsonKeys::Height, physSize.height()}};
        info[JsonKeys::DevicePixelRatio] = screen->devicePixelRatio();
        info[JsonKeys::RefreshRate] = screen->refreshRate();
        info[JsonKeys::Depth] = screen->depth();
        info[JsonKeys::ScreenId] = screenId;
        info[JsonKeys::SerialNumber] = screen->serialNumber();
        if (isVirtual) {
            info[JsonKeys::IsVirtualScreen] = true;
            info[JsonKeys::PhysicalScreenId] = physId;
            // Include the user-facing display name (e.g. "Left", "Right")
            if (vsIndex >= 0 && vsIndex < vsConfig.screens.size()) {
                info[JsonKeys::VirtualDisplayName] = vsConfig.screens[vsIndex].displayName;
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
    QString physId;
    if (!m_primaryScreenOverride.isEmpty()) {
        QScreen* overrideScreen = Utils::findScreenByName(m_primaryScreenOverride);
        if (overrideScreen) {
            physId = Utils::screenIdentifier(overrideScreen);
        }
    }
    if (physId.isEmpty()) {
        auto* primary = Utils::primaryScreen();
        if (!primary) {
            return QString();
        }
        physId = Utils::screenIdentifier(primary);
    }
    // If the primary monitor is subdivided into virtual screens, return the
    // first effective (virtual) screen ID so callers can use it for layout lookups.
    auto* mgr = ScreenManager::instance();
    if (mgr && mgr->hasVirtualScreens(physId)) {
        const QStringList vsIds = mgr->virtualScreenIdsFor(physId);
        if (!vsIds.isEmpty()) {
            return vsIds.first();
        }
    }
    return physId;
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

    // Enforce upper bound from ConfigDefaults
    if (config.screens.size() > ConfigDefaults::maxVirtualScreensPerPhysical()) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: too many virtual screens" << config.screens.size() << "(max"
                          << ConfigDefaults::maxVirtualScreensPerPhysical() << ")";
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
