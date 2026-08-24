// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BarController.h"

#include "QmlComponentBarWidgetFactory.h"

#include <PhosphorRegistry/RegistryNotifier.h>

#include <QDebug>
#include <QQmlEngine>
#include <QQuickItem>
#include <QStringList>

#include <memory>

using namespace PhosphorRegistry;

namespace PhosphorShellApp {

namespace {

// Registry::ids() is documented as registration (insertion) order. The
// sort imposes a stable ALPHABETICAL display order on top of that, so a
// consumer enumerating ids renders the same sequence regardless of the
// order the built-ins happened to be registered in.
// The registry id is stashed on the widget so the activation relay can
// recover it from sender(), rather than keeping a parallel map that would
// have to be pruned as widgets die.
constexpr auto kWidgetIdProperty = "_barWidgetId";

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
        {QStringLiteral("workspaces"), QStringLiteral("Workspaces"), QStringLiteral("Workspaces")},
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
    }
    return widget;
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

QStringList BarController::factoryIds() const
{
    return m_cachedIds;
}

} // namespace PhosphorShellApp
