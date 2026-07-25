// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/Registry.h>

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
// it lists. This is the first production registry owner (the Phase 3
// OSD/registry demos kept theirs in the demo executables); the bar QML
// stays registry-agnostic and talks to this through duck-typed methods,
// wired in as the `BarRegistry` context property by src/shell/main.cpp.
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
    explicit BarController(QObject* parent = nullptr);
    ~BarController() override;

    // Slot.qml provider contract: build the bar-widget delegate for `id`,
    // parented into `parent`. Returns null for an unknown id (the slot
    // skips it) or when no engine is resolvable from `parent`. The engine
    // is resolved from `parent` so QML need not pass it.
    [[nodiscard]] Q_INVOKABLE QQuickItem* createWidgetFor(const QString& id, QQuickItem* parent);

    // The registered widget ids, sorted for a deterministic order. Exposed
    // for introspection and a future layout/config editor; the default bar
    // layout drives slots from explicit ordered id lists, not this set.
    [[nodiscard]] QStringList factoryIds() const;

Q_SIGNALS:
    void factoryIdsChanged();

private:
    void registerBuiltins();

    PhosphorRegistry::Registry<PhosphorRegistry::IBarWidgetFactory> m_registry;
};

} // namespace PhosphorShellApp
