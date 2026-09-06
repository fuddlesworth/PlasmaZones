// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/IOSDFactory.h>
#include <PhosphorRegistry/Registry.h>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QQuickItem;
QT_END_NAMESPACE

namespace PhosphorShellApp {

// QML-exposed provider for the Phosphor.OSD surface, the shell's owner of
// what examples/phosphor-osd-demo keeps a private copy of. Owns a
// Registry<IOSDFactory>, registers the four built-in OSDs at construction,
// and exposes createOSD(kind, parent) so OSDHost can be wired as
// `provider: OsdRegistry` (the context property src/shell/main.cpp
// installs on every engine). Same shape as BarController: the OSD QML
// stays registry-agnostic and duck-types this.
//
// It is also the fan-out for triggers. There is one OSDHost per output,
// each built by PerScreenPanels in a context that cannot see shell.qml's
// ids, so the `osd` IpcTarget at the root has no direct path to them.
// Every host attaches itself here and show() forwards to all of them;
// OSDHost's own targetScreen filter decides which ones draw. Hosts are
// held through QPointer so a hot reload that destroys them leaves no
// dangling entry.
class OsdController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList factoryIds READ factoryIds NOTIFY factoryIdsChanged)

public:
    /// One built-in OSD: the registry id (the `kind` a trigger names), the
    /// label a browser shows, and the QML type inside moduleUri().
    struct BuiltinOsd
    {
        QString id;
        QString displayName;
        QString typeName;
    };

    /// The QML module the built-in delegates live in.
    [[nodiscard]] static QString moduleUri();

    /// The shipped catalogue, the single source of truth registerBuiltins
    /// registers from. The type names are string bindings to QML files in
    /// another tree, so only a test can catch a rename.
    [[nodiscard]] static const QList<BuiltinOsd>& builtinOsds();

    explicit OsdController(QObject* parent = nullptr);
    ~OsdController() override;

    // OSDHost provider contract: build the OSD delegate for `kind`,
    // parented into `parent`. Returns null for an unknown kind, a null
    // parent, or when no engine is resolvable from `parent`.
    [[nodiscard]] Q_INVOKABLE QQuickItem* createOSD(const QString& kind, QQuickItem* parent);

    // The registered kinds, sorted for a deterministic order.
    [[nodiscard]] QStringList factoryIds() const;

    // The underlying registry: the seam a plugin loader registers dynamic
    // OSDs through.
    [[nodiscard]] PhosphorRegistry::Registry<PhosphorRegistry::IOSDFactory>& registry();

    // Host fan-out. A host is any object with OSDHost's
    // show(kind, value, active, targetScreen) -> bool. Attaching twice is
    // a no-op; a dead host is dropped on the next show().
    Q_INVOKABLE void attachHost(QObject* host);
    Q_INVOKABLE void detachHost(QObject* host);
    [[nodiscard]] int hostCount() const;

    // Trigger an OSD on every attached host. `targetScreen` "" means every
    // screen, otherwise only the host with that screen name draws (the
    // host applies the filter). For the stateful kinds (mic, caps) a value
    // of 0 reads as off and non-zero as on; value-based kinds ignore that.
    // Returns true when at least one host accepted the trigger, so an
    // unknown kind reports failure over the wire.
    Q_INVOKABLE bool show(const QString& kind, int value, const QString& targetScreen = QString());

Q_SIGNALS:
    void factoryIdsChanged();

private:
    void registerBuiltins();
    void refreshFactoryIds();

    PhosphorRegistry::Registry<PhosphorRegistry::IOSDFactory> m_registry;
    QStringList m_cachedIds;
    QList<QPointer<QObject>> m_hosts;
};

} // namespace PhosphorShellApp
