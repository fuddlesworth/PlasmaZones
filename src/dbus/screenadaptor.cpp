// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenadaptor.h"
#include "dbushelpers.h"
#include "../config/settings.h"
#include "../config/settingsconfigstore.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/screenmanagerservice.h"
#include "../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorScreens/Swapper.h>
#include <QGuiApplication>
#include <QScreen>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>

namespace PlasmaZones {

ScreenAdaptor::~ScreenAdaptor() = default;

ScreenAdaptor::ScreenAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent)
{
    // Connect to screen change signals
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        const QString physId = Utils::screenIdentifier(screen);

        // Any new screen invalidates the cache — a new entry for physId
        // may need serializing and previously-missing siblings gain an
        // "isVirtualScreen" field once ScreenManager catches up.
        invalidateScreenInfoCache();

        // When virtual screens exist for this physical screen, emit only the
        // virtual screen IDs — not the physical ID — to avoid phantom entries.
        bool hadVirtual = emitForEffectiveScreens(physId, [this](const QString& id) {
            Q_EMIT screenAdded(id);
        });
        if (hadVirtual) {
            auto* mgr2 = screenManager();
            if (mgr2) {
                m_lastEmittedEffectiveIds = mgr2->effectiveScreenIds();
            }
            Q_EMIT virtualScreensChanged(physId);
        } else {
            // ScreenManager may not be fully initialized yet — defer a re-check
            // so virtual screen signals are emitted once the event loop settles.
            QTimer::singleShot(0, this, [this, physId]() {
                auto* mgr = screenManager();
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
        connect(screen, &QScreen::geometryChanged, screen, [this, screen, cachedId]() {
            handleScreenGeometryChanged(screen, cachedId);
        });

        // Cache effective screen IDs in a member map so they can be updated when
        // virtualScreensChanged fires. When screenRemoved fires, ScreenManager may
        // have already cleaned up virtual screen data, so we rely on cached IDs.
        auto* mgrForCache = screenManager();
        if (mgrForCache && mgrForCache->hasVirtualScreens(physId)) {
            m_cachedEffectiveIdsPerScreen[physId] = mgrForCache->virtualScreenIdsFor(physId);
        }

        // Use screen as the context object so the connection is automatically
        // destroyed when Qt deletes the QScreen on removal. This prevents dead
        // lambda accumulation on repeated dock/undock cycles.
        connect(qGuiApp, &QGuiApplication::screenRemoved, screen, [this, screen, cachedId](QScreen* removedScreen) {
            handleScreenRemoved(removedScreen, screen, cachedId);
        });
    });

    // Connect existing screens
    for (auto* screen : Utils::allScreens()) {
        // Cache screen ID at connection time to avoid querying a partially-destroyed QScreen.
        const QString cachedId = Utils::screenIdentifier(screen);
        connect(screen, &QScreen::geometryChanged, screen, [this, screen, cachedId]() {
            handleScreenGeometryChanged(screen, cachedId);
        });

        // Cache effective screen IDs in member map (same rationale as screenAdded above).
        auto* mgrForExisting = screenManager();
        if (mgrForExisting && mgrForExisting->hasVirtualScreens(cachedId)) {
            m_cachedEffectiveIdsPerScreen[cachedId] = mgrForExisting->virtualScreenIdsFor(cachedId);
        }

        connect(qGuiApp, &QGuiApplication::screenRemoved, screen, [this, screen, cachedId](QScreen* removedScreen) {
            handleScreenRemoved(removedScreen, screen, cachedId);
        });
    }

    // Update cached effective IDs when virtual screen config changes,
    // so screenRemoved signals emit the current IDs, not stale ones.
    // Also re-broadcast the D-Bus virtualScreensChanged signal so out-of-process
    // listeners (KWin effect) refetch their virtual screen state at runtime.
    // Without this, runtime add/remove only updates the daemon — the effect keeps
    // stale defs until something else triggers fetchAllVirtualScreenConfigs.
    if (auto* mgr = screenManager()) {
        auto refreshAndEmit = [this](const QString& physId) {
            invalidateScreenInfoCache();
            auto* m = screenManager();
            if (m && m->hasVirtualScreens(physId)) {
                m_cachedEffectiveIdsPerScreen[physId] = m->virtualScreenIdsFor(physId);
            } else {
                m_cachedEffectiveIdsPerScreen.remove(physId);
            }
            Q_EMIT virtualScreensChanged(physId);
        };
        connect(mgr, &ScreenManager::virtualScreensChanged, this, refreshAndEmit);
        // Regions-only changes also need the D-Bus broadcast so out-of-process
        // listeners (KWin effect) refresh their cached VS geometry.
        connect(mgr, &ScreenManager::virtualScreenRegionsChanged, this, refreshAndEmit);
    }
}

void ScreenAdaptor::handleScreenGeometryChanged(QScreen* screen, const QString& physId)
{
    Q_UNUSED(screen)
    invalidateScreenInfoCache();
    emitForEffectiveScreens(physId, [this](const QString& id) {
        Q_EMIT screenGeometryChanged(id);
    });
}

void ScreenAdaptor::handleScreenRemoved(QScreen* removedScreen, QScreen* targetScreen, const QString& cachedId)
{
    if (removedScreen != targetScreen) {
        return;
    }
    invalidateScreenInfoCache();
    const QStringList effectiveIds = m_cachedEffectiveIdsPerScreen.take(cachedId);
    if (!effectiveIds.isEmpty()) {
        for (const QString& id : effectiveIds) {
            Q_EMIT screenRemoved(id);
        }
    } else {
        Q_EMIT screenRemoved(cachedId);
    }
}

void ScreenAdaptor::invalidateScreenInfoCache()
{
    m_cachedScreenInfoJson.clear();
}

bool ScreenAdaptor::emitForEffectiveScreens(const QString& physId, const std::function<void(const QString&)>& emitFn)
{
    auto* mgr = screenManager();
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
    return effectiveScreenIdsWithFallback();
}

QString ScreenAdaptor::getScreenInfo(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(lcDbus) << "getScreenInfo: empty screen name";
        return QString();
    }

    // Serve memoized JSON when we have it — KCM and settings-app refreshes
    // poll getScreenInfo for every monitor on every page render, and the
    // underlying data only changes on screen topology signals (hot-plug,
    // geometry change, virtual screen reconfigure), each of which calls
    // invalidateScreenInfoCache(). The cached string is the fully-formed
    // D-Bus reply so we skip both the QScreen walk and JSON serialization.
    auto cachedIt = m_cachedScreenInfoJson.constFind(screenId);
    if (cachedIt != m_cachedScreenInfoJson.constEnd()) {
        return cachedIt.value();
    }

    // For virtual screens, resolve the backing physical screen for metadata
    // (manufacturer, model, refresh rate) and use ScreenManager for geometry
    auto* mgr = screenManager();
    const QScreen* screen = DbusHelpers::resolvePhysicalQScreen(screenId);

    if (screen) {
        // Use virtual screen geometry if available, otherwise physical
        QRect geom = screen->geometry();
        if (mgr && PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
            QRect vsGeom = mgr->screenGeometry(screenId);
            if (vsGeom.isValid()) {
                geom = vsGeom;
            }
        }

        QJsonObject info;
        info[::PhosphorZones::ZoneJsonKeys::Name] = screen->name();
        info[JsonKeys::Manufacturer] = screen->manufacturer();
        info[JsonKeys::Model] = screen->model();
        info[JsonKeys::Geometry] = QJsonObject{{::PhosphorZones::ZoneJsonKeys::X, geom.x()},
                                               {::PhosphorZones::ZoneJsonKeys::Y, geom.y()},
                                               {::PhosphorZones::ZoneJsonKeys::Width, geom.width()},
                                               {::PhosphorZones::ZoneJsonKeys::Height, geom.height()}};
        // Scale physical size proportionally for virtual screens.
        // A VS covering width=0.5 of the physical monitor gets half the mm width.
        QSizeF physSize = screen->physicalSize();
        const bool isVirtual = PhosphorIdentity::VirtualScreenId::isVirtual(screenId);
        const QString physId = isVirtual ? PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId) : QString();
        const int vsIndex = isVirtual ? PhosphorIdentity::VirtualScreenId::extractIndex(screenId) : -1;
        const Phosphor::Screens::VirtualScreenConfig vsConfig =
            (isVirtual && mgr) ? mgr->virtualScreenConfig(physId) : Phosphor::Screens::VirtualScreenConfig();
        if (isVirtual && vsIndex >= 0 && vsIndex < vsConfig.screens.size()) {
            const QRectF& region = vsConfig.screens[vsIndex].region;
            physSize = QSizeF(physSize.width() * region.width(), physSize.height() * region.height());
        }
        info[JsonKeys::PhysicalSize] = QJsonObject{{::PhosphorZones::ZoneJsonKeys::Width, physSize.width()},
                                                   {::PhosphorZones::ZoneJsonKeys::Height, physSize.height()}};
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

        const QString json = QString::fromUtf8(QJsonDocument(info).toJson());
        m_cachedScreenInfoJson.insert(screenId, json);
        return json;
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
    auto* mgr = screenManager();
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

QRect ScreenAdaptor::getAvailableGeometry(const QString& screenId)
{
    // Virtual screens: use ScreenManager's VS-aware available geometry
    auto* mgr = screenManager();
    if (mgr && PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return mgr->screenAvailableGeometry(screenId);
    }

    QScreen* screen = nullptr;
    if (!screenId.isEmpty()) {
        screen = Utils::findScreenByIdOrName(screenId);
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return QRect();
    }
    return actualAvailableGeometry(screen);
}

QRect ScreenAdaptor::getScreenGeometry(const QString& screenId)
{
    auto* mgr = screenManager();
    if (mgr) {
        QRect geo = mgr->screenGeometry(screenId);
        if (geo.isValid()) {
            return geo;
        }
    }
    // Fallback for physical screens when ScreenManager is unavailable
    QScreen* screen = screenId.isEmpty() ? QGuiApplication::primaryScreen() : Utils::findScreenByIdOrName(screenId);
    return screen ? screen->geometry() : QRect();
}

QString ScreenAdaptor::getVirtualScreenConfig(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcDbus) << "getVirtualScreenConfig: empty physicalScreenId";
        return QString();
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(physicalScreenId)) {
        qCWarning(lcDbus) << "getVirtualScreenConfig: expected physical screen ID, got virtual:" << physicalScreenId;
        return QString();
    }

    auto* mgr = screenManager();
    if (!mgr) {
        qCWarning(lcDbus) << "getVirtualScreenConfig: no ScreenManager instance";
        return QString();
    }

    Phosphor::Screens::VirtualScreenConfig config = mgr->virtualScreenConfig(physicalScreenId);

    QJsonObject root;
    root[JsonKeys::PhysicalScreenId] = physicalScreenId;

    QJsonArray screensArr;
    for (const auto& vs : config.screens) {
        QJsonObject screenObj;
        screenObj[::PhosphorZones::ZoneJsonKeys::Id] = vs.id;
        screenObj[JsonKeys::Index] = vs.index;
        screenObj[JsonKeys::DisplayName] = vs.displayName;
        screenObj[JsonKeys::Region] = QJsonObject{{::PhosphorZones::ZoneJsonKeys::X, vs.region.x()},
                                                  {::PhosphorZones::ZoneJsonKeys::Y, vs.region.y()},
                                                  {::PhosphorZones::ZoneJsonKeys::Width, vs.region.width()},
                                                  {::PhosphorZones::ZoneJsonKeys::Height, vs.region.height()}};
        screensArr.append(screenObj);
    }
    root[JsonKeys::Screens] = screensArr;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void ScreenAdaptor::setSettings(Settings* settings)
{
    m_settings = settings;
    // Build the IConfigStore facade lazily here — ScreenAdaptor is
    // constructed before Settings exists, so we can't wire this in the
    // ctor. Reset rather than reuse to handle a (rare but possible) re-wire
    // in tests.
    m_virtualScreenStore = settings ? std::make_unique<SettingsConfigStore>(settings) : nullptr;
}

void ScreenAdaptor::setVirtualScreenConfig(const QString& physicalScreenId, const QString& configJson)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: empty physicalScreenId";
        return;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(physicalScreenId)) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: expected physical screen ID, got virtual:" << physicalScreenId;
        return;
    }

    if (!m_settings) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: no Settings instance — adaptor was not wired";
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

    // Empty screens array is a removal request — write an empty config to
    // Settings; the daemon's virtualScreenConfigsChanged bridge tears down
    // ScreenManager subdivisions for this physical screen.
    if (screensArr.isEmpty()) {
        Phosphor::Screens::VirtualScreenConfig empty;
        empty.physicalScreenId = physicalScreenId;
        m_settings->setVirtualScreenConfig(physicalScreenId, empty);
        return;
    }

    Phosphor::Screens::VirtualScreenConfig config;
    config.physicalScreenId = physicalScreenId;

    // Translate the JSON payload into a Phosphor::Screens::VirtualScreenDef list. Do not
    // pre-validate region bounds, indices, or duplicates here — Settings is
    // the single point of admission control and runs the canonical
    // Phosphor::Screens::VirtualScreenConfig::isValid check which covers all those cases. A
    // local pre-filter would add a second validation layer with subtly
    // different log messages and rules to keep in sync.
    for (const auto& entry : screensArr) {
        QJsonObject screenObj = entry.toObject();
        QJsonObject regionObj = screenObj[JsonKeys::Region].toObject();

        Phosphor::Screens::VirtualScreenDef def;
        def.index = screenObj[JsonKeys::Index].toInt();
        def.id = PhosphorIdentity::VirtualScreenId::make(physicalScreenId, def.index);
        def.physicalScreenId = physicalScreenId;
        def.displayName = screenObj[JsonKeys::DisplayName].toString();
        def.region = QRectF(regionObj[::PhosphorZones::ZoneJsonKeys::X].toDouble(),
                            regionObj[::PhosphorZones::ZoneJsonKeys::Y].toDouble(),
                            regionObj[::PhosphorZones::ZoneJsonKeys::Width].toDouble(),
                            regionObj[::PhosphorZones::ZoneJsonKeys::Height].toDouble());
        config.screens.append(def);
    }

    // Persist to Settings (the source of truth). Settings::setVirtualScreenConfig
    // runs Phosphor::Screens::VirtualScreenConfig::isValid (region bounds, overlap, coverage,
    // duplicate ids/indices, max-per-physical) and emits
    // virtualScreenConfigsChanged on success. Rejected configs return false
    // and log the specific reason; we surface the failure to the caller's
    // D-Bus log so a misbehaving client can see why its write didn't stick.
    if (!m_settings->setVirtualScreenConfig(physicalScreenId, config)) {
        qCWarning(lcDbus) << "setVirtualScreenConfig: Settings rejected config for" << physicalScreenId;
    }
}

QString ScreenAdaptor::swapVirtualScreenInDirection(const QString& currentVirtualScreenId, const QString& direction)
{
    if (!m_settings) {
        qCWarning(lcDbus) << "swapVirtualScreenInDirection: no Settings instance — adaptor was not wired";
        return Phosphor::Screens::VirtualScreenSwapper::reasonString(
            Phosphor::Screens::VirtualScreenSwapper::Result::SettingsRejected);
    }
    if (currentVirtualScreenId.isEmpty()) {
        qCDebug(lcDbus) << "swapVirtualScreenInDirection: empty currentVirtualScreenId";
        return Phosphor::Screens::VirtualScreenSwapper::reasonString(
            Phosphor::Screens::VirtualScreenSwapper::Result::NotVirtual);
    }
    if (direction != Utils::Direction::Left && direction != Utils::Direction::Right && direction != Utils::Direction::Up
        && direction != Utils::Direction::Down) {
        qCWarning(lcDbus) << "swapVirtualScreenInDirection: invalid direction" << direction;
        return Phosphor::Screens::VirtualScreenSwapper::reasonString(
            Phosphor::Screens::VirtualScreenSwapper::Result::InvalidDirection);
    }

    Phosphor::Screens::VirtualScreenSwapper swapper(m_virtualScreenStore.get());
    const auto result = swapper.swapInDirection(currentVirtualScreenId, direction);
    const QString reason = Phosphor::Screens::VirtualScreenSwapper::reasonString(result);
    qCDebug(lcDbus) << "swapVirtualScreenInDirection:" << currentVirtualScreenId << direction << "->" << reason;
    return reason;
}

QString ScreenAdaptor::rotateVirtualScreens(const QString& physicalScreenId, bool clockwise)
{
    if (!m_settings) {
        qCWarning(lcDbus) << "rotateVirtualScreens: no Settings instance — adaptor was not wired";
        return Phosphor::Screens::VirtualScreenSwapper::reasonString(
            Phosphor::Screens::VirtualScreenSwapper::Result::SettingsRejected);
    }
    if (physicalScreenId.isEmpty()) {
        qCDebug(lcDbus) << "rotateVirtualScreens: empty physicalScreenId";
        return Phosphor::Screens::VirtualScreenSwapper::reasonString(
            Phosphor::Screens::VirtualScreenSwapper::Result::NotVirtual);
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(physicalScreenId)) {
        qCWarning(lcDbus) << "rotateVirtualScreens: expected physical screen ID, got virtual:" << physicalScreenId;
        return Phosphor::Screens::VirtualScreenSwapper::reasonString(
            Phosphor::Screens::VirtualScreenSwapper::Result::NotVirtual);
    }

    Phosphor::Screens::VirtualScreenSwapper swapper(m_virtualScreenStore.get());
    const auto result = swapper.rotate(physicalScreenId, clockwise);
    const QString reason = Phosphor::Screens::VirtualScreenSwapper::reasonString(result);
    qCDebug(lcDbus) << "rotateVirtualScreens:" << physicalScreenId << "cw=" << clockwise << "->" << reason;
    return reason;
}

QStringList ScreenAdaptor::getPhysicalScreens()
{
    QStringList result;
    auto* mgr = screenManager();
    if (!mgr) {
        return result;
    }
    for (const auto* screen : mgr->screens()) {
        result.append(Utils::screenIdentifier(screen));
    }
    return result;
}

QString ScreenAdaptor::getEffectiveScreenAt(int x, int y)
{
    auto* mgr = screenManager();
    if (!mgr) {
        return QString();
    }
    return mgr->effectiveScreenAt(QPoint(x, y));
}

} // namespace PlasmaZones
