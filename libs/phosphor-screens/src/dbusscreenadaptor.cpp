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
#include <QJsonValue>
#include <QPoint>
#include <QScreen>
#include <QSet>
#include <QSizeF>
#include <QTimer>

#include <cmath>
#include <limits>

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

ScreenManager* DBusScreenAdaptor::screenManager() const
{
    return m_screenManager.data();
}

IConfigStore* DBusScreenAdaptor::configStore() const
{
    return m_configStore.data();
}

DBusScreenAdaptor::~DBusScreenAdaptor()
{
    // Intentionally empty — Qt breaks connections on either peer's
    // destruction, so disconnecting here would be redundant at best and UB
    // at worst. Earlier versions called disconnect() against a raw pointer
    // that could already have been destroyed in daemon shutdown ordering
    // (QObject children are deleted AFTER the derived class's data members,
    // so a Daemon-owned ScreenManager held by unique_ptr is gone before a
    // Daemon-child ScreenAdaptor's destructor runs). m_screenManager is
    // now QPointer-guarded for readers elsewhere.
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
    m_cachedEffectiveIdsPerScreen.clear();
    if (m_screenManager) {
        connectScreenManagerSignals(m_screenManager);
        // Prime m_cachedEffectiveIdsPerScreen for screens that already exist.
        // The per-screen signal wiring in the constructor ran before the
        // manager was wired, so the cache population branch was skipped
        // there. Do it here instead — handleScreenRemoved relies on the
        // cache to emit per-VS screenRemoved tokens without a round-trip
        // through the (about-to-be-stale) manager.
        for (QScreen* screen : QGuiApplication::screens()) {
            const QString physId = ScreenIdentity::identifierFor(screen);
            if (m_screenManager->hasVirtualScreens(physId)) {
                m_cachedEffectiveIdsPerScreen[physId] = m_screenManager->virtualScreenIdsFor(physId);
            }
        }
        m_lastEmittedEffectiveIds = m_screenManager->effectiveScreenIds();
    } else {
        m_lastEmittedEffectiveIds.clear();
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
        // Refresh the effective-ids baseline on BOTH branches. The deferred
        // re-check (below) diffs against m_lastEmittedEffectiveIds to decide
        // whether a VS config arrived between this tick and the next event
        // loop turn — if we only updated the baseline on the hadVirtual
        // branch, the no-virtual path would leave a stale baseline that
        // could mis-compare against the new state and spuriously retract
        // (or suppress) the VS fan-out.
        if (m_screenManager) {
            m_lastEmittedEffectiveIds = m_screenManager->effectiveScreenIds();
        }
        if (hadVirtual) {
            Q_EMIT virtualScreensChanged(physId);
        } else {
            // ScreenManager may not be fully initialised yet — defer a re-check
            // so virtual screen signals are emitted once the event loop settles.
            // When the re-check finds VSs that weren't visible on the first
            // pass, we already emitted `screenAdded(physId)` above; retract it
            // with a matching `screenRemoved(physId)` before announcing the
            // virtuals, so D-Bus consumers never see a phantom physical
            // alongside its own VS children.
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
                    Q_EMIT screenRemoved(physId);
                    const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physId);
                    for (const QString& vsId : vsIds) {
                        Q_EMIT screenAdded(vsId);
                    }
                    // Cache so the later screen-removal handler knows to
                    // emit screenRemoved(vsId) for each VS instead of the
                    // physical ID we just retracted.
                    m_cachedEffectiveIdsPerScreen[physId] = vsIds;
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

    // Wire per-screen signals for currently-connected screens. Cache priming
    // keyed on m_screenManager happens in setScreenManager() — the manager
    // may not be wired yet when the constructor runs this loop. The
    // screen-removed lambda is still installed here so consumer-visible
    // retraction works from the moment the adaptor exists.
    for (QScreen* screen : QGuiApplication::screens()) {
        const QString cachedId = ScreenIdentity::identifierFor(screen);
        connect(screen, &QScreen::geometryChanged, screen, [this, screen, cachedId]() {
            handleScreenGeometryChanged(screen, cachedId);
        });
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
    // Explicit per-signal disconnects that mirror connectScreenManagerSignals —
    // keeps future ScreenManager signals from being silently severed by a
    // blanket disconnect(mgr, nullptr, this, nullptr). This policy matches
    // ScreenManager::stop()'s own disconnect discipline.
    disconnect(mgr, &ScreenManager::virtualScreensChanged, this, nullptr);
    disconnect(mgr, &ScreenManager::virtualScreenRegionsChanged, this, nullptr);
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
    QStringList effectiveIds = m_cachedEffectiveIdsPerScreen.take(cachedId);
    // Cold-cache fallback: VSs may have been configured for this screen
    // but `virtualScreensChanged` hadn't fired yet to populate
    // m_cachedEffectiveIdsPerScreen. Ask the manager directly so we emit
    // screenRemoved per-VS rather than a single screenRemoved(physId) that
    // leaves VS-tracking consumers holding stale IDs.
    if (effectiveIds.isEmpty() && m_screenManager && m_screenManager->hasVirtualScreens(cachedId)) {
        effectiveIds = m_screenManager->virtualScreenIdsFor(cachedId);
    }
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
        const QStringList ids = m_screenManager->virtualScreenIdsFor(physId);
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
    const VirtualScreenConfig vsConfig =
        (isVirtual && m_screenManager) ? m_screenManager->virtualScreenConfig(physId) : VirtualScreenConfig();
    // Resolve the VS def by exact id match — `index` is a semantic field and
    // is NOT guaranteed to match the array position (swap/rotate/external
    // writes can reorder the vector). A bare `vsConfig.screens[index]` would
    // silently return the wrong def after such a reorder.
    const VirtualScreenDef* vsDef = nullptr;
    if (isVirtual) {
        for (const auto& d : vsConfig.screens) {
            if (d.id == screenId) {
                vsDef = &d;
                break;
            }
        }
    }
    if (vsDef) {
        const QRectF& region = vsDef->region;
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
        if (vsDef) {
            info[kKeyVirtualDisplayName] = vsDef->displayName;
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

QString DBusScreenAdaptor::setVirtualScreenConfig(const QString& physicalScreenId, const QString& configJson)
{
    // Stable rejection tokens kept as local constants so consumers (KCM,
    // settings app) match on a single spelling even when the underlying
    // log wording changes.
    const auto reject = [](const char* token, const QString& detail) {
        qCWarning(lcPhosphorScreens) << "setVirtualScreenConfig:" << token << "—" << detail;
        return QString::fromUtf8(token);
    };

    if (physicalScreenId.isEmpty()) {
        return reject("empty_physical_id", QStringLiteral("physicalScreenId is empty"));
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(physicalScreenId)) {
        return reject("virtual_id_not_accepted",
                      QStringLiteral("expected physical screen ID, got virtual: %1").arg(physicalScreenId));
    }
    if (!m_configStore) {
        return reject("no_config_store", QStringLiteral("no IConfigStore wired"));
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(configJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return reject("parse_error", parseError.errorString());
    }
    if (!doc.isObject()) {
        return reject("not_object", QStringLiteral("JSON root is not an object"));
    }

    QJsonObject root = doc.object();
    const QJsonValue screensVal = root.value(kKeyScreens);
    if (!screensVal.isArray()) {
        return reject("missing_screens", QStringLiteral("missing or non-array 'screens' key"));
    }
    const QJsonArray screensArr = screensVal.toArray();

    if (screensArr.isEmpty()) {
        VirtualScreenConfig empty;
        empty.physicalScreenId = physicalScreenId;
        if (!m_configStore->save(physicalScreenId, empty)) {
            return reject("store_rejected", QStringLiteral("store rejected removal for %1").arg(physicalScreenId));
        }
        return QString();
    }

    // Reject a 1-entry screens[] up front. VirtualScreenConfig::hasSubdivisions
    // treats size<2 as "no subdivision", and Settings silently coerces such
    // a payload into a removal. Without this guard, a caller that sent a
    // 1-VS config over D-Bus sees their monitor's existing subdivision
    // erased with no error signal. Surface the rejection explicitly.
    if (screensArr.size() < 2) {
        return reject("too_few_screens",
                      QStringLiteral("screens[] has %1 entries; need at least 2 (send an empty array to remove)")
                          .arg(screensArr.size()));
    }

    VirtualScreenConfig config;
    config.physicalScreenId = physicalScreenId;

    auto finiteOrReject = [](double v) {
        return std::isfinite(v);
    };

    for (const auto& entry : screensArr) {
        if (!entry.isObject()) {
            return reject("bad_entry", QStringLiteral("screens[] entry is not an object"));
        }
        const QJsonObject screenObj = entry.toObject();
        const QJsonValue regionVal = screenObj.value(kKeyRegion);
        if (!regionVal.isObject()) {
            return reject("missing_region", QStringLiteral("entry missing 'region' object"));
        }
        const QJsonObject regionObj = regionVal.toObject();
        const double rx = regionObj.value(kKeyX).toDouble();
        const double ry = regionObj.value(kKeyY).toDouble();
        const double rw = regionObj.value(kKeyWidth).toDouble();
        const double rh = regionObj.value(kKeyHeight).toDouble();
        if (!finiteOrReject(rx) || !finiteOrReject(ry) || !finiteOrReject(rw) || !finiteOrReject(rh)) {
            return reject("non_finite_region", QStringLiteral("non-finite region value for %1").arg(physicalScreenId));
        }

        // Require an explicit integer `index`. A bare `.toInt()` silently returns 0
        // for missing/non-numeric values, which causes every malformed entry to
        // collide at index 0 and surface as a misleading "duplicate def.index 0"
        // error deeper in VirtualScreenConfig::isValid(). Reject up front with a
        // specific message instead.
        const QJsonValue indexVal = screenObj.value(kKeyIndex);
        if (!indexVal.isDouble()) {
            return reject("missing_index",
                          QStringLiteral("entry missing or non-numeric 'index' for %1").arg(physicalScreenId));
        }
        // JSON numbers are doubles; confirm the value is a non-negative integer.
        const double idxD = indexVal.toDouble();
        if (!std::isfinite(idxD) || idxD < 0.0 || idxD > static_cast<double>(std::numeric_limits<int>::max())
            || std::floor(idxD) != idxD) {
            return reject("bad_index",
                          QStringLiteral("'index' must be a non-negative integer, got %1 for %2")
                              .arg(idxD)
                              .arg(physicalScreenId));
        }

        VirtualScreenDef def;
        def.index = static_cast<int>(idxD);
        def.id = PhosphorIdentity::VirtualScreenId::make(physicalScreenId, def.index);
        def.physicalScreenId = physicalScreenId;
        def.displayName = screenObj.value(kKeyDisplayName).toString();
        def.region = QRectF(rx, ry, rw, rh);
        config.screens.append(def);
    }

    if (!m_configStore->save(physicalScreenId, config)) {
        return reject("store_rejected", QStringLiteral("store rejected config for %1").arg(physicalScreenId));
    }
    return QString();
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

    // Direction + id validation is delegated to VirtualScreenSwapper, which
    // already surfaces InvalidDirection / NotVirtual via its Result enum. A
    // duplicate check here buys no defense in depth (the swapper is the only
    // caller that can mutate state) and used to drift whenever the accepted
    // direction set was extended. VirtualScreenSwapper holds only a non-owning
    // IConfigStore*, so per-call construction is free.
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

    VirtualScreenSwapper swapper(m_configStore);
    const auto result = swapper.rotate(physicalScreenId, clockwise);
    const QString reason = VirtualScreenSwapper::reasonString(result);
    qCDebug(lcPhosphorScreens) << "rotateVirtualScreens:" << physicalScreenId << "cw=" << clockwise << "->" << reason;
    return reason;
}

} // namespace Phosphor::Screens
