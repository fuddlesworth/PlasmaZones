// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>
// Full definition for the ONE type this header's moc surface exposes: the
// `connection` Q_PROPERTY carries PipeWireConnection* (Qt6 moc
// auto-registers property metatypes, and QMetaType SFINAE-probes
// completeness — a fwd decl would re-fire GCC's -Wsfinae-incomplete once
// moc aggregation order stops shielding it). PwNode.h is NOT moc surface
// (the `node` role travels inside a QVariant through data()); it is
// included purely for consumers of that role, and PipeWireConnection.h
// already pulls it in anyway.
#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QStringList>

#include <memory>

namespace PhosphorServicePipeWire {

/// Filtered view over a `PipeWireConnection`'s registry-surfaced
/// nodes, exposed as a QAbstractListModel for direct QML binding.
///
/// Construction is GUI-thread only. Set `connection` and `mediaClasses`,
/// and the model populates itself from the connection's current
/// snapshot plus stays live by listening to `nodeAdded` / `nodeRemoved`
/// and to each node's `infoChanged` / `propsChanged`.
///
/// Roles (the int values are pinned by smoke tests so QML code keyed
/// on the role-name strings stays stable across versions):
/// - `node`        (`Qt::UserRole + 1`) — the PwNode* itself; QML usually
///                                        binds through this for nested
///                                        access (`model.node.volumes[0]`).
/// - `id`          (`Qt::UserRole + 2`) — PipeWire global id (quint32).
/// - `name`        (`Qt::UserRole + 3`) — `node.name`.
/// - `nick`        (`Qt::UserRole + 4`) — `node.nick`.
/// - `description` (`Qt::UserRole + 5`) — `node.description`.
/// - `mediaClass`  (`Qt::UserRole + 6`) — e.g. `Audio/Sink`.
/// - `channelCount`(`Qt::UserRole + 7`) — channel count from
///                                        SPA_PROP_channelVolumes.
/// - `volumes`     (`Qt::UserRole + 8`) — `QList<qreal>` linear amplitudes.
/// - `muted`       (`Qt::UserRole + 9`) — bool.
/// - `display`     (`Qt::DisplayRole`)  — falls back through nick →
///                                        description → name so plain
///                                        `model.display` always has
///                                        something usable.
///
/// `mediaClasses` is a list so a model can show e.g. both Audio/Sink and
/// Audio/Source if needed; the convenience subclasses below pin a single
/// class each for the common cases.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PwNodeModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PwNodeModel)
    Q_PROPERTY(PhosphorServicePipeWire::PipeWireConnection* connection READ connection WRITE setConnection NOTIFY
                   connectionChanged)
    Q_PROPERTY(QStringList mediaClasses READ mediaClasses WRITE setMediaClasses NOTIFY mediaClassesChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    // DO NOT REORDER OR INSERT MID-LIST — smoke tests pin these values
    // for QML wire stability. New roles must be appended at the end
    // (`Qt::UserRole + 10`, ...) so the documented role-name → int
    // mapping stays stable for QML consumers that key on the integers
    // (Q_ENUM exposes them by name into QML, but the int values are
    // also reachable via `Qt.UserRole + N` patterns).
    enum Role {
        NodeRole = Qt::UserRole + 1,
        IdRole = Qt::UserRole + 2,
        NameRole = Qt::UserRole + 3,
        NickRole = Qt::UserRole + 4,
        DescriptionRole = Qt::UserRole + 5,
        MediaClassRole = Qt::UserRole + 6,
        ChannelCountRole = Qt::UserRole + 7,
        VolumesRole = Qt::UserRole + 8,
        MutedRole = Qt::UserRole + 9,
    };
    Q_ENUM(Role)

    explicit PwNodeModel(QObject* parent = nullptr);
    ~PwNodeModel() override;

    [[nodiscard]] PipeWireConnection* connection() const;
    [[nodiscard]] QStringList mediaClasses() const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

public Q_SLOTS:
    /// Attach the model to a `PipeWireConnection` (or detach by passing
    /// `nullptr`). Tears down every existing row, swaps the connection
    /// handle, then re-seeds from the new connection's current node
    /// snapshot — filtered through `mediaClasses` — and subscribes to
    /// `nodeAdded` / `nodeRemoved` so subsequent registry events keep
    /// the model live. Idempotent: passing the current connection
    /// pointer is a no-op (no signals fire, no rows churn), with one
    /// exception — when the previously-attached connection was destroyed
    /// externally (e.g. the caller deleted it without first calling
    /// `setConnection(nullptr)`), the internal `QPointer` clears and a
    /// subsequent `setConnection(nullptr)` triggers a one-shot
    /// `beginResetModel` to flush the now-dangling row caches. The
    /// `connectionChanged` notification is emitted AFTER the rebuild
    /// completes so any QML binding awakened by the signal observes a
    /// consistent state: `connection()` returns the new pointer AND
    /// the model's rows already describe it. This preserves the
    /// invariant that the model's rows always describe its current
    /// `connection` property — emitting before the rebuild would
    /// briefly expose "new connection but stale rows" to observers.
    void setConnection(PipeWireConnection* connection);
    /// Replace the media-class filter set. Tears down every existing
    /// row and re-seeds from the current connection's snapshot through
    /// the new filter; if no connection is attached, just records the
    /// filter for the next `setConnection(non-null)` call. Idempotent
    /// on the current filter list (no signals, no rebuild). If the
    /// previously-attached connection was destroyed externally (the
    /// QPointer auto-nulled silently), this slot ALSO emits
    /// `connectionChanged` as a catch-up NOTIFY for the implicit
    /// non-null → null transition; bindings on the `connection`
    /// property therefore re-evaluate even when the user only meant
    /// to change the filter. The
    /// `mediaClassesChanged` notification is emitted AFTER the rebuild
    /// for the same row-invariant reason as `setConnection` — bindings
    /// awakened by the signal see `mediaClasses()` returning the new
    /// list AND the rows already filtered to match.
    void setMediaClasses(const QStringList& classes);

Q_SIGNALS:
    void connectionChanged();
    void mediaClassesChanged();
    void countChanged();

private:
    class Private;
    // Private is befriended so it can drive the QAbstractListModel
    // protected row-event machinery (`beginInsertRows` /
    // `endInsertRows`, `beginRemoveRows` / `endRemoveRows`, `index()`)
    // and emit our `countChanged` signal from inside the rebuild path,
    // without forcing the rebuild logic to live on PwNodeModel itself.
    friend class Private;
    // Pinned subclasses (PwSinkModel / PwSourceModel / PwStreamModel)
    // seed the media-class filter directly into `d` from their
    // constructors rather than calling the public `setMediaClasses`
    // slot. At construction the model is empty (no connection, no
    // rows), so there's nothing for `setMediaClasses` to rebuild —
    // and emitting `mediaClassesChanged` from a constructor would
    // wake any binding before the subclass is fully constructed.
    // Direct assignment via friendship keeps the seeding contained
    // without exposing a public mutator that bypasses the rebuild.
    friend class PwSinkModel;
    friend class PwSourceModel;
    friend class PwStreamModel;
    std::unique_ptr<Private> d;
};

/// Convenience subclass pre-pinned to `Audio/Sink`. Designed for QML
/// `PwSinkModel { connection: PipeWireHost.connection }`. Mirrors the
/// pattern used in `phosphor-service-mpris`' `MprisPlayerModel`.
///
/// Copy/move suppression is inherited from `PwNodeModel`; redeclaring
/// `Q_DISABLE_COPY_MOVE` here would only add noise to the moc table
/// without changing the contract.
///
/// Not marked `final` even though the design rationale excludes further
/// subclassing: `qmlRegisterType<PwSinkModel>` (and the sibling
/// registrations below) instantiate Qt's internal
/// `QQmlPrivate::QQmlElement<T>`, which is declared `final class
/// QQmlElement final : public T` — that subclass cannot derive from a
/// `final` base, so adding `final` here breaks the build with
/// `cannot derive from 'final' base 'PwSinkModel'`. The "leaf convenience
/// wrapper" contract is enforced socially / via code review instead;
/// callers that need a different media-class filter should still
/// instantiate `PwNodeModel` directly and set `mediaClasses`.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PwSinkModel : public PwNodeModel
{
    Q_OBJECT

public:
    explicit PwSinkModel(QObject* parent = nullptr);
};

/// Convenience subclass pre-pinned to `Audio/Source`. Copy/move
/// suppression inherited from `PwNodeModel`. Same "no `final`" caveat
/// as `PwSinkModel` — `qmlRegisterType`'s internal `QQmlElement<T>`
/// derives from us.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PwSourceModel : public PwNodeModel
{
    Q_OBJECT

public:
    explicit PwSourceModel(QObject* parent = nullptr);
};

/// Convenience subclass pinned to BOTH `Stream/Output/Audio` and
/// `Stream/Input/Audio`. Streams are application-level audio endpoints
/// (Firefox's playback stream, the OBS capture stream, etc.) and the
/// mixer UI typically wants them in one list grouped by direction.
/// Copy/move suppression inherited from `PwNodeModel`. Same "no `final`"
/// caveat as `PwSinkModel`.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PwStreamModel : public PwNodeModel
{
    Q_OBJECT

public:
    explicit PwStreamModel(QObject* parent = nullptr);
};

} // namespace PhosphorServicePipeWire
