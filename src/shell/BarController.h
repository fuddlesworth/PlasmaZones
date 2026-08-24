// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/Registry.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorShellApp {

// QML-exposed provider for the Phosphor.Bar surface. Owns a
// Registry<IBarWidgetFactory>, registers the built-in bar widgets as
// IBarWidgetFactory instances at construction, and exposes
// createWidgetFor(id, parent) so Slot.qml can instantiate the delegates
// it lists. This is the first production owner of a UI-seam registry (the
// five shell surfaces); the domain registries — shader packs, animation
// effects, curves, tiling algorithms, layout sources — already compose
// Registry<T>. The bar QML stays registry-agnostic and talks to this
// through duck-typed methods, wired in as the `BarRegistry` context
// property by src/shell/main.cpp.
//
// The engine is resolved per call from the widget's parent
// (qmlEngine(parent)) rather than cached, so the controller survives the
// shell's hot-reload (each reload builds a fresh QQmlEngine) without a
// stale-engine contract. It is process-global (owned by main()), outliving
// every engine rebuild.
class BarController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList factoryIds READ factoryIds NOTIFY factoryIdsChanged)

public:
    /// One built-in bar widget: the registry id, the label a browser shows,
    /// and the QML type it resolves to inside moduleUri().
    struct BuiltinWidget
    {
        QString id;
        QString displayName;
        QString typeName;
    };

    /// The QML module the built-in delegates live in.
    [[nodiscard]] static QString moduleUri();

    /// The shipped catalogue, as the single source of truth: registerBuiltins
    /// registers exactly this, and the test suite resolves exactly this
    /// against the module. The type names are string bindings to QML files in
    /// another tree, so nothing but a test can catch a rename or a dropped
    /// QML_FILES entry, both of which compile and link cleanly and degrade to
    /// a widget that silently never appears.
    [[nodiscard]] static const QList<BuiltinWidget>& builtinWidgets();

    explicit BarController(QObject* parent = nullptr);
    ~BarController() override;

    // Slot.qml provider contract: build the bar-widget delegate for `id`,
    // parented into `parent`. Returns null for an unknown id (the slot
    // skips it), for a null `parent`, or when no engine is resolvable from
    // `parent`. The engine is resolved from `parent` so QML need not pass
    // it, which is also why `parent` must be non-null.
    //
    // A widget declaring an `activated()` signal is wired to widgetActivated
    // here, AFTER its construction completes. A widget that emits during its
    // own Component.onCompleted therefore fires before the relay exists and
    // is not heard; triggers must emit in response to input, not at birth.
    [[nodiscard]] Q_INVOKABLE QQuickItem* createWidgetFor(const QString& id, QQuickItem* parent);

    // The registered widget ids, sorted for a deterministic order. Exposed
    // for introspection and a future layout/config editor; the default bar
    // layout drives slots from explicit ordered id lists, not this set.
    [[nodiscard]] QStringList factoryIds() const;

Q_SIGNALS:
    void factoryIdsChanged();

    /// A bar widget was activated (clicked, Space/Enter, or an assistive-tech
    /// press), carrying the registry id of the widget and the widget itself.
    ///
    /// The trailing bar buttons — power, notifications, control centre — are
    /// only triggers; what they open lives in other modules. Routing through
    /// the registry keeps that edge out of Phosphor.Bar, which would
    /// otherwise have to depend on every surface it can summon. The shell
    /// composer listens here and decides what a given id opens.
    ///
    /// `source` matters because there is one bar per output: without it every
    /// screen's "power" button emits an identical payload and a bar-anchored
    /// popout has no way to know which bar it belongs to. A screen-centred
    /// popout can ignore it.
    void widgetActivated(const QString& id, QQuickItem* source);

private Q_SLOTS:
    /// Relay for a widget's QML-declared `activated` signal. QML signals have
    /// no compile-time member pointer, so the connection is made in the
    /// string-based form, which requires a real slot.
    void relayWidgetActivation();

private:
    void registerBuiltins();
    // Re-reads the registry and emits factoryIdsChanged only when the id
    // set actually differs from the last emission.
    void refreshFactoryIds();

    PhosphorRegistry::Registry<PhosphorRegistry::IBarWidgetFactory> m_registry;
    // The id list QML reads. Seeded at construction and refreshed only when
    // the registry's contents actually change, so factoryIds() is a cheap
    // read rather than a re-sort per binding evaluation, and a notification
    // that leaves the set unchanged emits nothing.
    QStringList m_cachedIds;
};

} // namespace PhosphorShellApp
