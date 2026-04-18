// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/DBusScreenAdaptor.h"

#include "PhosphorScreens/IConfigStore.h"
#include "PhosphorScreens/Manager.h"
#include "PhosphorScreens/ScreenIdentity.h"
#include "PhosphorScreens/Swapper.h"
#include "PhosphorScreens/VirtualScreen.h"
#include "screenslogging.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPoint>
#include <QScreen>
#include <QSet>
#include <QSizeF>
#include <QTimer>

namespace Phosphor::Screens {

namespace {
// JSON key constants matching the existing PlasmaZones wire format so
// the KCM / settings app continue to deserialise what the adaptor sends.
// Promoted to lib-internal constants here (vs. the daemon's JsonKeys::*)
// so the lib is self-contained.
constexpr QLatin1String kKeyName{"name"};
constexpr QLatin1String kKeyManufacturer{"manufacturer"};
constexpr QLatin1String kKeyModel{"model"};
constexpr QLatin1String kKeyGeometry{"geometry"};
constexpr QLatin1String kKeyPhysicalSize{"physicalSize"};
constexpr QLatin1String kKeyDevicePixelRatio{"devicePixelRatio"};
constexpr QLatin1String kKeyRefreshRate{"refreshRate"};
constexpr QLatin1String kKeyDepth{"depth"};
constexpr QLatin1String kKeyScreenId{"screenId"};
constexpr QLatin1String kKeySerialNumber{"serialNumber"};
constexpr QLatin1String kKeyIsVirtualScreen{"isVirtualScreen"};
constexpr QLatin1String kKeyPhysicalScreenId{"physicalScreenId"};
constexpr QLatin1String kKeyVirtualDisplayName{"virtualDisplayName"};
constexpr QLatin1String kKeyIndex{"index"};
constexpr QLatin1String kKeyDisplayName{"displayName"};
constexpr QLatin1String kKeyRegion{"region"};
constexpr QLatin1String kKeyScreens{"screens"};
constexpr QLatin1String kKeyX{"x"};
constexpr QLatin1String kKeyY{"y"};
constexpr QLatin1String kKeyWidth{"width"};
constexpr QLatin1String kKeyHeight{"height"};
constexpr QLatin1String kKeyId{"id"};
} // namespace

DBusScreenAdaptor::DBusScreenAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent)
{
    wireQGuiApplicationSignals();
}

DBusScreenAdaptor::~DBusScreenAdaptor()
{
    if (m_screenManager) {
        disconnectScreenManagerSignals(m_screenManager);
    }
}

void DBusScreenAdaptor::setScreenManager(ScreenManager* manager)
{
    if (m_screenManager == manager) {
        return;
    }
    if (m_screenManager) {
        disconnectScreenManagerSignals(m_screenManager);
    }
    m_screenManager = manager;
    if (m_screenManager) {
        connectScreenManagerSignals(m_screenManager);
    }
    invalidateScreenInfoCache();
}

void DBusScreenAdaptor::setConfigStore(IConfigStore* store)
{
    m_configStore = store;
}

void DBusScreenAdaptor::wireQGuiApplicationSignals()
{
    if (m_qGuiAppSignalsWired) {
        return;
    }
    m_qGuiAppSignalsWired = true;

    // Connect to screen-added — track per-screen geometry signals + emit D-Bus
    // screenAdded for either the virtual IDs or the physical ID.
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        const QString physId = ScreenIdentity::identifierFor(screen);
        invalidateScreenInfoCache();

        bool hadVirtual = emitForEffectiveScreens(physId, [this](const QString& id) {
            Q_EMIT screenAdded(id);
        });
        if (hadVirtual) {
            if (m_screenManager) {
                m_lastEmittedEffectiveIds = m_screenManager->effectiveScreenIds();
            }
            Q_EMIT virtualScreensChanged(physId);
        } else {
            // ScreenManager may not be fully initialised yet — defer a re-check
            // so virtual screen signals are emitted once the event loop settles.
            QTimer::singleShot(0, this, [this, physId]() {
                if (!m_screenManager) {
                    return;
                }
                QStringList currentEffective = m_screenManager->effectiveScreenIds();
                if (currentEffective == m_lastEmittedEffectiveIds) {
                    return;
                }
                m_lastEmittedEffectiveIds = currentEffective;

                if (m_screenManager->hasVirtualScreens(physId)) {
                    for (const QString& vsId : m_screenManager->virtualScreenIdsFor(physId)) {
                        Q_EMIT screenAdded(vsId);
                    }
                    Q_EMIT virtualScreensChanged(physId);
                }
            });
        }

        const QString cachedId = physId;
        connect(screen, &QScreen::geometryChanged, screen, [this, screen, cachedId]() {
            handleScreenGeometryChanged(screen, cachedId);
        });

        if (m_screenManager && m_screenManager->hasVirtualScreens(physId)) {
            m_cachedEffectiveIdsPerScreen[physId] = m_screenManager->virtualScreenIdsFor(physId);
        }

        connect(qGuiApp, &QGuiApplication::screenRemoved, screen, [this, screen, cachedId](QScreen* removedScreen) {
            handleScreenRemoved(removedScreen, screen, cachedId);
        });
    });

    // Connect existing screens
    for (QScreen* screen : QGuiApplication::screens()) {
        const QString cachedId = ScreenIdentity::identifierFor(screen);
        connect(screen, &QScreen::geometryChanged, screen, [this, screen, cachedId]() {
            handleScreenGeometryChanged(screen, cachedId);
        });
        if (m_screenManager && m_screenManager->hasVirtualScreens(cachedId)) {
            m_cachedEffectiveIdsPerScreen[cachedId] = m_screenManager->virtualScreenIdsFor(cachedId);
        }
        connect(qGuiApp, &QGuiApplication::screenRemoved, screen, [this, screen, cachedId](QScreen* removedScreen) {
            handleScreenRemoved(removedScreen, screen, cachedId);
        });
    }
}

void DBusScreenAdaptor::connectScreenManagerSignals(ScreenManager* mgr)
{
    if (!mgr) {
        return;
    }
    auto refreshAndEmit = [this](const QString& physId) {
        invalidateScreenInfoCache();
        if (m_screenManager && m_screenManager->hasVirtualScreens(physId)) {
            m_cachedEffectiveIdsPerScreen[physId] = m_screenManager->virtualScreenIdsFor(physId);
        } else {
            m_cachedEffectiveIdsPerScreen.remove(physId);
        }
        Q_EMIT virtualScreensChanged(physId);
    };
    connect(mgr, &ScreenManager::virtualScreensChanged, this, refreshAndEmit);
    connect(mgr, &ScreenManager::virtualScreenRegionsChanged, this, refreshAndEmit);
}

void DBusScreenAdaptor::disconnectScreenManagerSignals(ScreenManager* mgr)
{
    if (!mgr) {
        return;
    }
    disconnect(mgr, nullptr, this, nullptr);
}

void DBusScreenAdaptor::handleScreenGeometryChanged(QScreen* /*screen*/, const QString& physId)
{
    invalidateScreenInfoCache();
    emitForEffectiveScreens(physId, [this](const QString& id) {
        Q_EMIT screenGeometryChanged(id);
    });
}

void DBusScreenAdaptor::handleScreenRemoved(QScreen* removedScreen, QScreen* targetScreen, const QString& cachedId)
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

void DBusScreenAdaptor::invalidateScreenInfoCache()
{
    m_cachedScreenInfoJson.clear();
}

bool DBusScreenAdaptor::emitForEffectiveScreens(const QString& physId,
                                                const std::function<void(const QString&)>& emitFn)
{
    if (m_screenManager) {
        bool hadVirtual = m_screenManager->hasVirtualScreens(physId);
        const QStringList ids = m_screenManager->effectiveIdsForPhysical(physId);
        for (const QString& id : ids) {
            emitFn(id);
        }
        return hadVirtual;
    }
    emitFn(physId);
    return false;
}

QStringList DBusScreenAdaptor::getScreens()
{
    if (m_screenManager) {
        QStringList ids = m_screenManager->effectiveScreenIds();
        if (!ids.isEmpty()) {
            return ids;
        }
    }
    QStringList result;
    for (QScreen* screen : QGuiApplication::screens()) {
        result.append(ScreenIdentity::identifierFor(screen));
    }
    return result;
}

QString DBusScreenAdaptor::getScreenInfo(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(lcPhosphorScreens) << "getScreenInfo: empty screen name";
        return QString();
    }

    auto cachedIt = m_cachedScreenInfoJson.constFind(screenId);
    if (cachedIt != m_cachedScreenInfoJson.constEnd()) {
        return cachedIt.value();
    }

    QScreen* screen = nullptr;
    const bool isVirtual = PhosphorIdentity::VirtualScreenId::isVirtual(screenId);
    const QString physId = isVirtual ? PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId) : screenId;
    screen = ScreenIdentity::findByIdOrName(physId);
    if (!screen) {
        qCWarning(lcPhosphorScreens) << "Screen not found:" << screenId;
        return QString();
    }

    QRect geom = screen->geometry();
    if (m_screenManager && isVirtual) {
        QRect vsGeom = m_screenManager->screenGeometry(screenId);
        if (vsGeom.isValid()) {
            geom = vsGeom;
        }
    }

    QJsonObject info;
    info[kKeyName] = screen->name();
    info[kKeyManufacturer] = screen->manufacturer();
    info[kKeyModel] = screen->model();
    info[kKeyGeometry] =
        QJsonObject{{kKeyX, geom.x()}, {kKeyY, geom.y()}, {kKeyWidth, geom.width()}, {kKeyHeight, geom.height()}};

    QSizeF physSize = screen->physicalSize();
    const int vsIndex = isVirtual ? PhosphorIdentity::VirtualScreenId::extractIndex(screenId) : -1;
    const VirtualScreenConfig vsConfig =
        (isVirtual && m_screenManager) ? m_screenManager->virtualScreenConfig(physId) : VirtualScreenConfig();
    if (isVirtual && vsIndex >= 0 && vsIndex < vsConfig.screens.size()) {
        const QRectF& region = vsConfig.screens[vsIndex].region;
        physSize = QSizeF(physSize.width() * region.width(), physSize.height() * region.height());
    }
    info[kKeyPhysicalSize] = QJsonObject{{kKeyWidth, physSize.width()}, {kKeyHeight, physSize.height()}};
    info[kKeyDevicePixelRatio] = screen->devicePixelRatio();
    info[kKeyRefreshRate] = screen->refreshRate();
    info[kKeyDepth] = screen->depth();
    info[kKeyScreenId] = screenId;
    info[kKeySerialNumber] = screen->serialNumber();
    if (isVirtual) {
        info[kKeyIsVirtualScreen] = true;
        info[kKeyPhysicalScreenId] = physId;
        if (vsIndex >= 0 && vsIndex < vsConfig.screens.size()) {
            info[kKeyVirtualDisplayName] = vsConfig.screens[vsIndex].displayName;
        }
    }

    const QString json = QString::fromUtf8(QJsonDocument(info).toJson());
    m_cachedScreenInfoJson.insert(screenId, json);
    return json;
}

QString DBusScreenAdaptor::getPrimaryScreen()
{
    QString physId;
    if (!m_primaryScreenOverride.isEmpty()) {
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screen->name() == m_primaryScreenOverride) {
                physId = ScreenIdentity::identifierFor(screen);
                break;
            }
        }
    }
    if (physId.isEmpty()) {
        QScreen* primary = QGuiApplication::primaryScreen();
        if (!primary) {
            return QString();
        }
        physId = ScreenIdentity::identifierFor(primary);
    }
    if (m_screenManager && m_screenManager->hasVirtualScreens(physId)) {
        const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physId);
        if (!vsIds.isEmpty()) {
            return vsIds.first();
        }
    }
    return physId;
}

void DBusScreenAdaptor::setPrimaryScreenFromKWin(const QString& connectorName)
{
    m_primaryScreenOverride = connectorName;
    qCInfo(lcPhosphorScreens) << "Primary screen override set from compositor:" << connectorName;
}

QString DBusScreenAdaptor::getScreenId(const QString& connectorName)
{
    if (connectorName.isEmpty()) {
        return QString();
    }
    return ScreenIdentity::idForName(connectorName);
}

QRect DBusScreenAdaptor::getAvailableGeometry(const QString& screenId)
{
    if (m_screenManager && PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return m_screenManager->screenAvailableGeometry(screenId);
    }

    QScreen* screen = nullptr;
    if (!screenId.isEmpty()) {
        screen = ScreenIdentity::findByIdOrName(screenId);
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return QRect();
    }
    return m_screenManager ? m_screenManager->actualAvailableGeometry(screen) : screen->availableGeometry();
}

QRect DBusScreenAdaptor::getScreenGeometry(const QString& screenId)
{
    if (m_screenManager) {
        QRect geo = m_screenManager->screenGeometry(screenId);
        if (geo.isValid()) {
            return geo;
        }
    }
    QScreen* screen = screenId.isEmpty() ? QGuiApplication::primaryScreen() : ScreenIdentity::findByIdOrName(screenId);
    return screen ? screen->geometry() : QRect();
}

QString DBusScreenAdaptor::getVirtualScreenConfig(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcPhosphorScreens) << "getVirtualScreenConfig: empty physicalScreenId";
        return QString();
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(physicalScreenId)) {
        qCWarning(lcPhosphorScreens) << "getVirtualScreenConfig: expected physical screen ID, got virtual:"
                                     << physicalScreenId;
        return QString();
    }
    if (!m_screenManager) {
        qCWarning(lcPhosphorScreens) << "getVirtualScreenConfig: no ScreenManager wired";
        return QString();
    }

    VirtualScreenConfig config = m_screenManager->virtualScreenConfig(physicalScreenId);

    QJsonObject root;
    root[kKeyPhysicalScreenId] = physicalScreenId;

    QJsonArray screensArr;
    for (const auto& vs : config.screens) {
        QJsonObject screenObj;
        screenObj[kKeyId] = vs.id;
        screenObj[kKeyIndex] = vs.index;
        screenObj[kKeyDisplayName] = vs.displayName;
        screenObj[kKeyRegion] = QJsonObject{{kKeyX, vs.region.x()},
                                            {kKeyY, vs.region.y()},
                                            {kKeyWidth, vs.region.width()},
                                            {kKeyHeight, vs.region.height()}};
        screensArr.append(screenObj);
    }
    root[kKeyScreens] = screensArr;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void DBusScreenAdaptor::setVirtualScreenConfig(const QString& physicalScreenId, const QString& configJson)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcPhosphorScreens) << "setVirtualScreenConfig: empty physicalScreenId";
        return;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(physicalScreenId)) {
        qCWarning(lcPhosphorScreens) << "setVirtualScreenConfig: expected physical screen ID, got virtual:"
                                     << physicalScreenId;
        return;
    }
    if (!m_configStore) {
        qCWarning(lcPhosphorScreens) << "setVirtualScreenConfig: no IConfigStore wired";
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(configJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcPhosphorScreens) << "setVirtualScreenConfig: invalid JSON:" << parseError.errorString();
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray screensArr = root[kKeyScreens].toArray();

    if (screensArr.isEmpty()) {
        VirtualScreenConfig empty;
        empty.physicalScreenId = physicalScreenId;
        m_configStore->save(physicalScreenId, empty);
        return;
    }

    VirtualScreenConfig config;
    config.physicalScreenId = physicalScreenId;

    for (const auto& entry : screensArr) {
        QJsonObject screenObj = entry.toObject();
        QJsonObject regionObj = screenObj[kKeyRegion].toObject();

        VirtualScreenDef def;
        def.index = screenObj[kKeyIndex].toInt();
        def.id = PhosphorIdentity::VirtualScreenId::make(physicalScreenId, def.index);
        def.physicalScreenId = physicalScreenId;
        def.displayName = screenObj[kKeyDisplayName].toString();
        def.region = QRectF(regionObj[kKeyX].toDouble(), regionObj[kKeyY].toDouble(), regionObj[kKeyWidth].toDouble(),
                            regionObj[kKeyHeight].toDouble());
        config.screens.append(def);
    }

    if (!m_configStore->save(physicalScreenId, config)) {
        qCWarning(lcPhosphorScreens) << "setVirtualScreenConfig: store rejected config for" << physicalScreenId;
    }
}

QStringList DBusScreenAdaptor::getPhysicalScreens()
{
    QStringList result;
    for (QScreen* screen : QGuiApplication::screens()) {
        result.append(ScreenIdentity::identifierFor(screen));
    }
    return result;
}

QString DBusScreenAdaptor::getEffectiveScreenAt(int x, int y)
{
    if (!m_screenManager) {
        return QString();
    }
    return m_screenManager->effectiveScreenAt(QPoint(x, y));
}

QString DBusScreenAdaptor::swapVirtualScreenInDirection(const QString& currentVirtualScreenId, const QString& direction)
{
    if (!m_configStore) {
        qCWarning(lcPhosphorScreens) << "swapVirtualScreenInDirection: no IConfigStore wired";
        return VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::SettingsRejected);
    }
    if (currentVirtualScreenId.isEmpty()) {
        qCDebug(lcPhosphorScreens) << "swapVirtualScreenInDirection: empty currentVirtualScreenId";
        return VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::NotVirtual);
    }
    if (direction != Direction::Left && direction != Direction::Right && direction != Direction::Up
        && direction != Direction::Down) {
        qCWarning(lcPhosphorScreens) << "swapVirtualScreenInDirection: invalid direction" << direction;
        return VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::InvalidDirection);
    }

    VirtualScreenSwapper swapper(m_configStore);
    const auto result = swapper.swapInDirection(currentVirtualScreenId, direction);
    const QString reason = VirtualScreenSwapper::reasonString(result);
    qCDebug(lcPhosphorScreens) << "swapVirtualScreenInDirection:" << currentVirtualScreenId << direction << "->"
                               << reason;
    return reason;
}

QString DBusScreenAdaptor::rotateVirtualScreens(const QString& physicalScreenId, bool clockwise)
{
    if (!m_configStore) {
        qCWarning(lcPhosphorScreens) << "rotateVirtualScreens: no IConfigStore wired";
        return VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::SettingsRejected);
    }
    if (physicalScreenId.isEmpty()) {
        qCDebug(lcPhosphorScreens) << "rotateVirtualScreens: empty physicalScreenId";
        return VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::NotVirtual);
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(physicalScreenId)) {
        qCWarning(lcPhosphorScreens) << "rotateVirtualScreens: expected physical screen ID, got virtual:"
                                     << physicalScreenId;
        return VirtualScreenSwapper::reasonString(VirtualScreenSwapper::Result::NotVirtual);
    }

    VirtualScreenSwapper swapper(m_configStore);
    const auto result = swapper.rotate(physicalScreenId, clockwise);
    const QString reason = VirtualScreenSwapper::reasonString(result);
    qCDebug(lcPhosphorScreens) << "rotateVirtualScreens:" << physicalScreenId << "cw=" << clockwise << "->" << reason;
    return reason;
}

} // namespace Phosphor::Screens
