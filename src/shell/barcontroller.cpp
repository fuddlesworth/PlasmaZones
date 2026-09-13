// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BarController.h"

#include "QmlComponentBarWidgetFactory.h"

#include <PhosphorRegistry/RegistryNotifier.h>

#include <QDebug>
#include <QGuiApplication>
#include <QPointF>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QStringList>

#include <memory>

using namespace PhosphorRegistry;

namespace PhosphorShellApp {

namespace {

// The registry id is stashed on the widget so the activation relay can
// recover it from sender(), rather than keeping a parallel map that would
// have to be pruned as widgets die.
constexpr auto kWidgetIdProperty = "_barWidgetId";

// Registry::ids() is documented as registration (insertion) order. The
// sort imposes a stable ALPHABETICAL display order on top of that, so a
// consumer enumerating ids renders the same sequence regardless of the
// order the built-ins happened to be registered in.
QStringList sortedIds(const Registry<IBarWidgetFactory>& registry)
{
    QStringList ids = registry.ids();
    ids.sort();
    return ids;
}
} // namespace

QString BarController::moduleUri()
{
    return QStringLiteral("Phosphor.Bar");
}

const QList<BarController::BuiltinWidget>& BarController::builtinWidgets()
{
    // The display names are untranslated. IFactoryBase leaves translation to
    // the factory ("may be translated by the factory implementation"), no
    // surface renders them yet, and the shell tier has no i18n wiring at all
    // on either side (no PhosphorI18n link here, no localized context for
    // QML). They become PhosphorI18n::tr() calls together with the rest of
    // the shell's strings when that story lands; translating these alone
    // would localise the one label nothing displays.
    static const QList<BuiltinWidget> widgets{
        {QStringLiteral("clock"), QStringLiteral("Clock"), QStringLiteral("Clock")},
        {QStringLiteral("focusedapp"), QStringLiteral("Focused App"), QStringLiteral("FocusedApp")},
        {QStringLiteral("placementmap"), QStringLiteral("Placement Map"), QStringLiteral("PlacementMap")},
        {QStringLiteral("systemmetrics"), QStringLiteral("System Metrics"), QStringLiteral("SystemMetrics")},
        {QStringLiteral("network"), QStringLiteral("Network"), QStringLiteral("Network")},
        {QStringLiteral("bluetooth"), QStringLiteral("Bluetooth"), QStringLiteral("Bluetooth")},
        {QStringLiteral("audio"), QStringLiteral("Audio"), QStringLiteral("Audio")},
        {QStringLiteral("battery"), QStringLiteral("Battery"), QStringLiteral("Battery")},
        {QStringLiteral("tray"), QStringLiteral("System Tray"), QStringLiteral("Tray")},
        {QStringLiteral("media"), QStringLiteral("Media"), QStringLiteral("Media")},
        {QStringLiteral("controlcenter"), QStringLiteral("Control Center"), QStringLiteral("ControlCenterButton")},
        {QStringLiteral("notification"), QStringLiteral("Notifications"), QStringLiteral("NotificationButton")},
        {QStringLiteral("power"), QStringLiteral("Power"), QStringLiteral("PowerButton")},
        {QStringLiteral("spacer"), QStringLiteral("Spacer"), QStringLiteral("Spacer")},
    };
    return widgets;
}

BarController::BarController(QObject* parent)
    : QObject(parent)
{
    // Register the built-ins BEFORE wiring the notifier forwarding, so the
    // one-time construction registrations don't each fire a no-op
    // factoryIdsChanged (nothing is connected yet, and QML reads factoryIds
    // fresh when it binds).
    registerBuiltins();
    m_cachedIds = sortedIds(m_registry);

    // Both registry notifications route through one value-comparing slot.
    // Nothing mutates this registry after construction today, so neither
    // signal currently fires; the guard is here for the dynamic path a
    // plugin loader would add. Under DuplicatePolicy::Replace a
    // re-registration fires factoryUnregistered then factoryRegistered for
    // the SAME id, and forwarding each straight to factoryIdsChanged would
    // emit twice for an id set that never changed, against the "only emit
    // when the value actually changes" rule.
    QObject::connect(m_registry.notifier(), &RegistryNotifier::factoryRegistered, this,
                     &BarController::refreshFactoryIds);
    QObject::connect(m_registry.notifier(), &RegistryNotifier::factoryUnregistered, this,
                     &BarController::refreshFactoryIds);
}

PhosphorRegistry::Registry<PhosphorRegistry::IBarWidgetFactory>& BarController::registry()
{
    return m_registry;
}

void BarController::refreshFactoryIds()
{
    QStringList ids = sortedIds(m_registry);
    if (ids == m_cachedIds) {
        return;
    }
    m_cachedIds = std::move(ids);
    Q_EMIT factoryIdsChanged();
}

BarController::~BarController() = default;

void BarController::registerBuiltins()
{
    // Register each entry of the shared catalogue as an IBarWidgetFactory
    // wrapping a delegate type from the Phosphor.Bar module. Capabilities are
    // advisory (enforced in Phase 5). Registration iterates builtinWidgets()
    // so it cannot drift from the catalogue. The unit test's copy of the id
    // list is deliberately hand-written rather than derived from the same
    // accessor, so adding or dropping a widget has to be an explicit edit in
    // two places and cannot pass unnoticed.
    for (const BuiltinWidget& widget : builtinWidgets()) {
        const bool registered = m_registry.registerFactory(std::make_shared<QmlComponentBarWidgetFactory>(
            widget.id, widget.displayName, moduleUri(), widget.typeName, QStringList{QStringLiteral("bar.widget")}));
        if (!registered) {
            // Only reachable by giving two built-ins the same id, which
            // would silently drop one from every bar. Surface it here
            // rather than leaving it to Registry's internal warning.
            qCWarning(lcBar) << "BarController: duplicate built-in bar widget id" << widget.id << "was rejected";
        }
    }
}

QQuickItem* BarController::createWidgetFor(const QString& id, QQuickItem* parent)
{
    const auto factory = m_registry.factory(id);
    if (!factory) {
        qCWarning(lcBar) << "BarController: no bar widget registered for id" << id;
        return nullptr;
    }
    // Distinct messages: a null parent is a QML wiring bug, while a live
    // parent with no engine is a shutdown race. One shared message makes
    // the log useless for telling them apart.
    if (!parent) {
        qCWarning(lcBar) << "BarController: null parent for id" << id;
        return nullptr;
    }
    QQmlEngine* engine = qmlEngine(parent);
    if (!engine) {
        qCWarning(lcBar) << "BarController: no QML engine resolvable from parent for id" << id;
        return nullptr;
    }
    QQuickItem* widget = factory->createWidget(engine, parent);
    if (!widget) {
        return nullptr;
    }
    // Duck-typed: only some widgets are triggers. The metaobject lookup is
    // what keeps this from being a hard contract every delegate must satisfy
    // — a widget with no `activated` signal simply is not connected.
    if (widget->metaObject()->indexOfSignal("activated()") >= 0) {
        // Verify by READING BACK, never by setProperty's return value.
        // setProperty returns false whenever the name is not a declared
        // Q_PROPERTY — it stores a dynamic property and reports false — and
        // `_barWidgetId` is dynamic by design, so treating that as failure
        // would skip the connect for every widget, always. This exact
        // mistake shipped once and left every bar button inert.
        //
        // The read-back is still worth doing: a delegate that declares its own
        // property of this name takes the write and may coerce it (an int
        // property yields "0"), which would relay a bogus id.
        widget->setProperty(kWidgetIdProperty, id);
        if (widget->property(kWidgetIdProperty).toString() != id) {
            qCWarning(lcBar) << "BarController: widget for id" << id << "declares its own" << kWidgetIdProperty
                             << "property; leaving it unconnected rather than relaying a bogus id";
            return widget;
        }
        QObject::connect(widget, SIGNAL(activated()), this, SLOT(relayWidgetActivation()));
        m_triggers.insert(id, QPointer<QQuickItem>(widget));
    }
    return widget;
}

bool BarController::activateWidget(const QString& id)
{
    // Drop the dead entries as they are met; a bar that unmounted takes
    // its widgets with it.
    for (auto it = m_triggers.find(id); it != m_triggers.end() && it.key() == id;) {
        if (it.value().isNull()) {
            it = m_triggers.erase(it);
            continue;
        }
        Q_EMIT widgetActivated(id, it.value().data());
        return true;
    }
    qCDebug(lcBar) << "BarController: no live trigger widget for id" << id;
    return false;
}

void BarController::relayWidgetActivation()
{
    auto* widget = qobject_cast<QQuickItem*>(sender());
    if (!widget) {
        // Unreachable today: the only connection is made in createWidgetFor
        // against the factory's QQuickItem*. Logged rather than dropped so the
        // day that stops being true is diagnosable.
        qCWarning(lcBar) << "BarController: activation from a non-item sender; ignoring";
        return;
    }
    const QString id = widget->property(kWidgetIdProperty).toString();
    if (id.isEmpty()) {
        // Only reachable if the delegate declares its own property of this
        // name and rejects the write. Silent here would mean a bar button
        // that does nothing with nothing in the log to explain it.
        qCWarning(lcBar) << "BarController: activated widget carries no registry id; ignoring";
        return;
    }
    Q_EMIT widgetActivated(id, widget);
}

QScreen* BarController::screenOf(QQuickItem* item) const
{
    QScreen* screen = nullptr;
    if (item) {
        if (const QQuickWindow* window = item->window()) {
            screen = window->screen();
        }
    }
    // An unresolved source should open the panel somewhere sensible rather
    // than nowhere: a null targetScreen leaves the transport with no bar to
    // hang from.
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    // LOAD-BEARING, and the same trap ControlCenterController::screenOf
    // documents at length: a QScreen has no QObject parent, so QML gives a
    // Q_INVOKABLE's parentless QObject* return JavaScriptOwnership and the
    // garbage collector DELETES the live screen when the wrapper is
    // collected. The screen belongs to QGuiApplication; say so.
    if (screen) {
        QQmlEngine::setObjectOwnership(screen, QQmlEngine::CppOwnership);
    }
    return screen;
}

qreal BarController::anchorCenterFor(QQuickItem* item) const
{
    if (!item) {
        return -1.0;
    }
    QQuickWindow* window = item->window();
    if (!window) {
        // A widget built but not yet shown. The caller falls back to a
        // fixed bar anchor rather than placing the panel at the origin.
        return -1.0;
    }
    // Item -> scene, scene -> global, global -> screen-local. The last hop
    // is what makes this correct on a multi-head desktop: mapToGlobal
    // returns a virtual-desktop coordinate, and PopoutHost places against a
    // surface that is full-bleed on ONE output, whose origin is the screen's
    // geometry topLeft.
    const QPointF sceneCenter = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
    const QPointF globalCenter = window->mapToGlobal(sceneCenter);
    const QScreen* screen = window->screen();
    if (!screen) {
        return -1.0;
    }
    return globalCenter.x() - screen->geometry().x();
}

void BarController::setOpenPanel(const QString& id, QQuickItem* source)
{
    if (m_openPanelId == id && m_openPanelSource == source) {
        return;
    }
    m_openPanelId = id;
    m_openPanelSource = source;
    Q_EMIT openPanelChanged();
}

QString BarController::openPanelId() const
{
    return m_openPanelId;
}

qreal BarController::openPanelAnchorX() const
{
    // Recomputed on read rather than cached at setOpenPanel: the chip's
    // position moves as the bar lays out (a widget appearing or collapsing
    // shifts every chip after it), and a tether pinned to where the chip
    // was when the panel opened would drift off it.
    return m_openPanelId.isEmpty() ? -1.0 : anchorCenterFor(m_openPanelSource.data());
}

QString BarController::openPanelScreen() const
{
    if (m_openPanelId.isEmpty() || !m_openPanelSource) {
        return {};
    }
    const QScreen* screen = screenOf(m_openPanelSource.data());
    return screen ? screen->name() : QString();
}

QStringList BarController::factoryIds() const
{
    return m_cachedIds;
}

} // namespace PhosphorShellApp
