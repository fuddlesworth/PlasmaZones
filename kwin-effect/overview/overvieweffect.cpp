// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overvieweffect.h"

#include "daemonclient.h"
#include "overviewlogging.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorIdentity/ScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <core/output.h>
#include <virtualdesktops.h>

#include <QDBusConnection>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScreen>
#include <QTimer>
#include <QUuid>

namespace PlasmaZones::Overview {

OverviewEffect::OverviewEffect()
    : m_state(new KWin::EffectTogglableState(this))
    , m_shutdownTimer(new QTimer(this))
    , m_daemon(new DaemonClient(this))
{
    // Until the daemon's settings land, the global motion base is the same
    // 300 ms KWin's own Overview animates with.
    m_globalMotion.duration = m_animationDuration;
    auto* gesture = new KWin::EffectTogglableGesture(m_state);
    gesture->addTouchpadSwipeGesture(KWin::SwipeDirection::Up, 4);
    gesture->addTouchscreenSwipeGesture(KWin::SwipeDirection::Up, 3);

    connect(m_state, &KWin::EffectTogglableState::inProgressChanged, this, &OverviewEffect::gestureInProgressChanged);
    connect(m_state, &KWin::EffectTogglableState::partialActivationFactorChanged, this,
            &OverviewEffect::partialActivationFactorChanged);
    connect(m_state, &KWin::EffectTogglableState::statusChanged, this,
            [this](KWin::EffectTogglableState::Status status) {
                using Status = KWin::EffectTogglableState::Status;
                if (status == Status::Activating || status == Status::Active) {
                    tryStart();
                }
                if (status == Status::Inactive) {
                    deactivate();
                }
            });

    // Desktop names: KWin owns them, the label reads them through
    // desktopName() and re-reads on every revision bump.
    const auto desktops = KWin::effects->desktops();
    for (KWin::VirtualDesktop* desktop : desktops) {
        watchDesktopName(desktop);
    }
    connect(KWin::effects, &KWin::EffectsHandler::desktopAdded, this, [this](KWin::VirtualDesktop* desktop) {
        watchDesktopName(desktop);
        bumpDesktopNames();
    });
    connect(KWin::effects, &KWin::EffectsHandler::desktopRemoved, this, [this](KWin::VirtualDesktop*) {
        bumpDesktopNames();
    });

    // The live per-output desktop swipe, shown inside an open overview. A
    // programmatic setCurrentDesktop emits only desktopChanged, so this is
    // purely for the touchpad gesture.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanging, this,
            [this](KWin::VirtualDesktop*, QPointF offset, KWin::EffectWindow*, KWin::LogicalOutput* output) {
                m_screenDesktopOffsets.insert(output, offset);
                Q_EMIT desktopOffsetChanged(output);
            });
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, this,
            [this](KWin::VirtualDesktop*, KWin::VirtualDesktop*, KWin::EffectWindow*, KWin::LogicalOutput* output) {
                m_screenDesktopOffsets.insert(output, QPointF(0, 0));
                Q_EMIT desktopOffsetChanged(output);
            });
    connect(KWin::effects, &KWin::EffectsHandler::desktopChangingCancelled, this, [this]() {
        m_screenDesktopOffsets.clear();
        const auto screens = KWin::effects->screens();
        for (KWin::LogicalOutput* output : screens) {
            Q_EMIT desktopOffsetChanged(output);
        }
    });
    // An output added or removed while open: close now, the daemon fosters
    // the workspaces and the next open renders the result. The screen-id
    // cache is keyed by connector and the duplicate-model suffix depends on
    // the whole set, so it is dropped on either edge.
    connect(KWin::effects, &KWin::EffectsHandler::screenAdded, this, [this](KWin::LogicalOutput*) {
        m_screenIdCache.clear();
        deactivateNow();
    });
    connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, this, [this](KWin::LogicalOutput* output) {
        m_screenIdCache.clear();
        m_screenDesktopOffsets.remove(output);
        deactivateNow();
    });
    connect(KWin::effects, &KWin::EffectsHandler::screenAboutToLock, this, &OverviewEffect::deactivateNow);

    m_shutdownTimer->setSingleShot(true);
    connect(m_shutdownTimer, &QTimer::timeout, this, [this]() {
        if (m_state->status() == KWin::EffectTogglableState::Status::Inactive) {
            setRunning(false);
        }
    });

    connect(m_daemon, &DaemonClient::toggleRequested, this, &OverviewEffect::toggle);
    connect(m_daemon, &DaemonClient::closeRequested, this, [this]() {
        if (m_state->status() != KWin::EffectTogglableState::Status::Inactive) {
            deactivate();
        }
    });
    connect(m_daemon, &DaemonClient::workspaceMapChanged, this, &OverviewEffect::workspaceMapChanged);
    connect(m_daemon, &DaemonClient::modelChanged, this, &OverviewEffect::overviewModelChanged);
    connect(m_daemon, &DaemonClient::availableChanged, this, [this](bool available) {
        Q_EMIT daemonAvailableChanged();
        if (available) {
            loadSettings();
        }
    });
    // Settings edges: the global duration rides settingsChanged, the motion
    // tree its own signal (kept apart daemon-side so the settings app's
    // change detection is not reset by profile-file writes).
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::Settings,
                                          QStringLiteral("settingsChanged"), this, SLOT(loadSettings()));
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::Settings,
                                          QStringLiteral("motionProfileTreeChanged"), this, SLOT(loadSettings()));
    if (m_daemon->isAvailable()) {
        loadSettings();
    }

    auto* delegate = new QQmlComponent(KWin::effects->qmlEngine(), this);
    connect(delegate, &QQmlComponent::statusChanged, this, [delegate]() {
        if (delegate->isError()) {
            qCWarning(lcOverview) << "Failed to load the overview QML:" << delegate->errorString();
        }
    });
    // The module is embedded in this plugin's binary and registered under
    // qrc:/qt/qml, which the engine's default import path covers. The
    // explicit qrc URL is the same file; loading by URL rather than by
    // module name sidesteps the engine's import-path cache, which does not
    // rescan for a module registered after the engine's first import.
    delegate->loadUrl(QUrl(QStringLiteral("qrc:/qt/qml/org/plasmazones/overview/qml/Main.qml")),
                      QQmlComponent::Asynchronous);
    setDelegate(delegate);
}

OverviewEffect::~OverviewEffect() = default;

int OverviewEffect::requestedEffectChainPosition() const
{
    return 70;
}

qreal OverviewEffect::partialActivationFactor() const
{
    return m_state->partialActivationFactor();
}

bool OverviewEffect::gestureInProgress() const
{
    return m_state->inProgress();
}

QVariantMap OverviewEffect::workspaceMap() const
{
    return m_daemon->workspaceMap();
}

QVariantMap OverviewEffect::overviewModel() const
{
    return m_daemon->model();
}

bool OverviewEffect::daemonAvailable() const
{
    return m_daemon->isAvailable();
}

QVariantMap OverviewEffect::initialProperties(KWin::LogicalOutput* screen)
{
    QVariantMap props;
    props.insert(QStringLiteral("screenId"), screenIdFor(screen));
    return props;
}

QString OverviewEffect::screenIdFor(KWin::LogicalOutput* output) const
{
    if (!output) {
        return QString();
    }
    const QString connectorName = output->name();
    const auto cached = m_screenIdCache.constFind(connectorName);
    if (cached != m_screenIdCache.constEnd()) {
        return cached.value();
    }
    // Same recipe as PlasmaZonesEffect::outputScreenId: the base id from the
    // output's own manufacturer / model / connector with the serial from the
    // matching QScreen, and a "/connector" suffix while another output
    // produces the same base id. A connector-keyed id here would name a
    // screen the daemon's map does not know.
    const auto serialFor = [](const QString& connector) {
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screen->name() == connector) {
                return screen->serialNumber();
            }
        }
        return QString();
    };
    const QString baseId = PhosphorIdentity::ScreenId::buildScreenBaseId(output->manufacturer(), output->model(),
                                                                         serialFor(connectorName), connectorName);
    bool hasDuplicate = false;
    for (const KWin::LogicalOutput* other : KWin::effects->screens()) {
        if (!other || other->name() == connectorName) {
            continue;
        }
        if (PhosphorIdentity::ScreenId::buildScreenBaseId(other->manufacturer(), other->model(),
                                                          serialFor(other->name()), other->name())
            == baseId) {
            hasDuplicate = true;
            break;
        }
    }
    const QString result = hasDuplicate ? baseId + QLatin1Char('/') + connectorName : baseId;
    m_screenIdCache.insert(connectorName, result);
    return result;
}

KWin::EffectWindow* OverviewEffect::windowFor(const QString& windowId) const
{
    // Daemon window ids are the composite "appId|instanceId"; the instance
    // half is the compositor's internal UUID without braces.
    const QString instance = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    if (instance.isEmpty()) {
        return nullptr;
    }
    const QUuid uuid = QUuid::fromString(instance);
    if (uuid.isNull()) {
        return nullptr;
    }
    return KWin::effects->findWindow(uuid);
}

QVariant OverviewEffect::windowHandle(const QString& windowId) const
{
    KWin::EffectWindow* w = windowFor(windowId);
    return w ? QVariant::fromValue(w->internalId()) : QVariant();
}

bool OverviewEffect::windowExists(const QString& windowId) const
{
    return windowFor(windowId) != nullptr;
}

int OverviewEffect::stackingIndex(const QString& windowId) const
{
    KWin::EffectWindow* w = windowFor(windowId);
    if (!w) {
        return -1;
    }
    const QList<KWin::EffectWindow*> order = KWin::effects->stackingOrder();
    const qsizetype index = order.indexOf(w);
    // A window missing from the list sorts last, above everything listed.
    return index < 0 ? static_cast<int>(order.size()) : static_cast<int>(index);
}

void OverviewEffect::closeWindow(const QString& windowId)
{
    if (KWin::EffectWindow* w = windowFor(windowId)) {
        w->closeWindow();
    }
}

void OverviewEffect::activateWindow(const QString& windowId)
{
    if (KWin::EffectWindow* w = windowFor(windowId)) {
        KWin::effects->activateWindow(w);
    }
}

QPointF OverviewEffect::desktopOffsetForScreen(KWin::LogicalOutput* screen) const
{
    return m_screenDesktopOffsets.value(screen, QPointF(0, 0));
}

void OverviewEffect::focusWorkspace(const QString& screenId, const QString& desktopId)
{
    m_daemon->callOverview(QStringLiteral("focusWorkspace"), {screenId, desktopId});
}

void OverviewEffect::moveWindowToWorkspace(const QString& windowId, const QString& screenId, const QString& desktopId,
                                           int dropX, int dropY)
{
    m_daemon->callOverview(QStringLiteral("moveWindowToWorkspace"), {windowId, screenId, desktopId, dropX, dropY});
}

void OverviewEffect::moveWindowToNewWorkspace(const QString& windowId, const QString& screenId, int sliceIndex,
                                              int dropX, int dropY)
{
    m_daemon->callOverview(QStringLiteral("moveWindowToNewWorkspace"), {windowId, screenId, sliceIndex, dropX, dropY});
}

void OverviewEffect::reorderWorkspace(const QString& screenId, const QString& desktopId, int newSliceIndex)
{
    m_daemon->callOverview(QStringLiteral("reorderWorkspace"), {screenId, desktopId, newSliceIndex});
}

void OverviewEffect::moveWorkspaceToScreen(const QString& desktopId, const QString& targetScreenId, int sliceIndex)
{
    m_daemon->callOverview(QStringLiteral("moveWorkspaceToScreen"), {desktopId, targetScreenId, sliceIndex});
}

void OverviewEffect::renameWorkspace(const QString& desktopId, const QString& name)
{
    m_daemon->callOverview(QStringLiteral("renameWorkspace"), {desktopId, name});
}

void OverviewEffect::pinWorkspace(const QString& desktopId, bool pinned)
{
    m_daemon->callOverview(QStringLiteral("pinWorkspace"), {desktopId, pinned});
}

void OverviewEffect::panStrip(const QString& screenId, const QString& desktopId, int deltaPx)
{
    m_daemon->callOverview(QStringLiteral("panStrip"), {screenId, desktopId, deltaPx});
}

void OverviewEffect::tryStart()
{
    if (isRunning()) {
        return;
    }
    if (KWin::effects->isScreenLocked()) {
        m_state->deactivate();
        return;
    }
    // A swipe reaches here as an in-progress partial activation; the
    // shortcut and the daemon's toggle never do.
    if (!m_gestureEnabled && m_state->inProgress()) {
        m_state->deactivate();
        return;
    }
    // The data source is the workspace map; before adoption (or with the
    // feature off) it is empty and there is nothing honest to draw.
    if (!m_daemon->isAvailable() || m_daemon->workspaceMap().isEmpty()) {
        qCDebug(lcOverview) << "overview toggle ignored: daemon absent or no workspace map";
        m_state->deactivate();
        return;
    }
    setRunning(true);
    // setRunning(true) silently does nothing when another fullscreen effect
    // holds the slot or the keyboard grab fails; never assume it succeeded.
    if (!isRunning()) {
        qCDebug(lcOverview) << "overview refused: another fullscreen effect is active or the keyboard grab failed";
        m_daemon->reportOverviewState(false);
        m_state->deactivate();
        return;
    }
    m_shutdownTimer->stop();
    if (!m_daemonOpen) {
        m_daemonOpen = true;
        m_daemon->setOverviewOpen(true);
    }
}

void OverviewEffect::activate()
{
    if (KWin::effects->isScreenLocked()) {
        return;
    }
    m_state->activate();
}

void OverviewEffect::deactivate()
{
    m_shutdownTimer->start(animationDuration());
    m_state->deactivate();
    if (m_daemonOpen) {
        m_daemonOpen = false;
        m_daemon->setOverviewOpen(false);
    }
}

void OverviewEffect::deactivateNow()
{
    m_shutdownTimer->stop();
    m_state->deactivate();
    setRunning(false);
    if (m_daemonOpen) {
        m_daemonOpen = false;
        m_daemon->setOverviewOpen(false);
    }
}

void OverviewEffect::toggle()
{
    if (m_state->status() == KWin::EffectTogglableState::Status::Inactive) {
        activate();
    } else {
        deactivate();
    }
}

QString OverviewEffect::desktopName(const QString& desktopId) const
{
    const auto desktops = KWin::effects->desktops();
    for (KWin::VirtualDesktop* desktop : desktops) {
        if (desktop->id() == desktopId) {
            return desktop->name();
        }
    }
    return QString();
}

void OverviewEffect::watchDesktopName(KWin::VirtualDesktop* desktop)
{
    connect(desktop, &KWin::VirtualDesktop::nameChanged, this, &OverviewEffect::bumpDesktopNames);
}

void OverviewEffect::bumpDesktopNames()
{
    ++m_desktopNamesRevision;
    Q_EMIT desktopNamesRevisionChanged();
}

void OverviewEffect::loadSettings()
{
    using PhosphorProtocol::ClientHelpers::loadSettingAsync;
    loadSettingAsync(this, QStringLiteral("animationDuration"), [this](const QVariant& v) {
        bool ok = false;
        const int raw = v.toInt(&ok);
        if (!ok || raw <= 0) {
            return;
        }
        m_globalMotion.duration = raw;
        resolveAnimationDuration();
    });
    loadSettingAsync(this, PhosphorProtocol::Service::SettingProperty::MotionProfileTree, [this](const QVariant& v) {
        const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (!doc.isObject()) {
            return;
        }
        m_motionTree = PhosphorAnimation::ProfileTree::fromJson(doc.object(), m_curveRegistry);
        resolveAnimationDuration();
    });
    // Workspaces.Overview. The daemon's schema already clamps the zoom and
    // canonicalises the colour, so the guards here only cover a wire value
    // that failed to convert at all.
    loadSettingAsync(this, QStringLiteral("overviewZoom"), [this](const QVariant& v) {
        bool ok = false;
        const qreal zoom = v.toDouble(&ok);
        if (!ok || zoom <= 0.0 || zoom >= 1.0 || qFuzzyCompare(1.0 + zoom, 1.0 + m_zoom)) {
            return;
        }
        m_zoom = zoom;
        Q_EMIT zoomChanged();
    });
    loadSettingAsync(this, QStringLiteral("overviewBackdropColor"), [this](const QVariant& v) {
        const QColor color(v.toString());
        if (!color.isValid() || color == m_backdropColor) {
            return;
        }
        m_backdropColor = color;
        Q_EMIT backdropColorChanged();
    });
    loadSettingAsync(this, QStringLiteral("overviewGestureEnabled"), [this](const QVariant& v) {
        m_gestureEnabled = v.toBool();
    });
    loadSettingAsync(this, QStringLiteral("overviewWheelSwitchesWorkspaces"), [this](const QVariant& v) {
        const bool enabled = v.toBool();
        if (enabled == m_wheelSwitchesWorkspaces) {
            return;
        }
        m_wheelSwitchesWorkspaces = enabled;
        Q_EMIT wheelSwitchesWorkspacesChanged();
    });
    loadSettingAsync(this, QStringLiteral("overviewShowWorkspaceNames"), [this](const QVariant& v) {
        const bool enabled = v.toBool();
        if (enabled == m_showWorkspaceNames) {
            return;
        }
        m_showWorkspaceNames = enabled;
        Q_EMIT showWorkspaceNamesChanged();
    });
}

void OverviewEffect::resolveAnimationDuration()
{
    // The open/close motion is the desktop.switch node: global animator
    // profile as the base, the motion tree's override chain on top, exactly
    // the main effect's resolveEventMotionProfile minus the per-window rule
    // tier (the overview is a windowless event).
    const PhosphorAnimation::Profile resolved = m_motionTree.hasAnyOverride()
        ? m_motionTree.overlayChainOnto(PhosphorAnimation::ProfilePaths::DesktopSwitch, m_globalMotion)
        : m_globalMotion;
    setAnimationDuration(qRound(resolved.effectiveDuration()));
}

void OverviewEffect::setAnimationDuration(int duration)
{
    if (duration <= 0 || m_animationDuration == duration) {
        return;
    }
    m_animationDuration = duration;
    Q_EMIT animationDurationChanged();
}

} // namespace PlasmaZones::Overview

#include "moc_overvieweffect.cpp"
