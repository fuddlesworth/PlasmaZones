// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QStringList>

#include <memory>

namespace PhosphorServicePipeWire {

class PipeWireConnection;
class PwNode;

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
/// - `node`        (`Qt::UserRole+1`)  — the PwNode* itself; QML usually
///                                       binds through this for nested
///                                       access (`model.node.volumes[0]`).
/// - `id`          (`Qt::UserRole+2`)  — PipeWire global id (quint32).
/// - `name`        (`Qt::UserRole+3`)  — `node.name`.
/// - `nick`        (`Qt::UserRole+4`)  — `node.nick`.
/// - `description` (`Qt::UserRole+5`)  — `node.description`.
/// - `mediaClass`  (`Qt::UserRole+6`)  — e.g. `Audio/Sink`.
/// - `channelCount`(`Qt::UserRole+7`)  — channel count from
///                                       SPA_PROP_channelVolumes.
/// - `volumes`     (`Qt::UserRole+8`)  — `QList<qreal>` linear amplitudes.
/// - `muted`       (`Qt::UserRole+9`)  — bool.
/// - `display`     (`Qt::DisplayRole`) — falls back through nick →
///                                       description → name so plain
///                                       `model.display` always has
///                                       something usable.
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
    void setConnection(PipeWireConnection* connection);
    void setMediaClasses(const QStringList& classes);

Q_SIGNALS:
    void connectionChanged();
    void mediaClassesChanged();
    void countChanged();

private:
    /// Tear down all wires + rows, attach to `connection`, and re-seed
    /// from its current node snapshot. Used by both `setConnection`
    /// (which then emits `connectionChanged`) and `setMediaClasses`
    /// (which only emits `mediaClassesChanged`, since the connection
    /// pointer didn't actually change).
    void rebuildFromConnection(PipeWireConnection* connection);

    class Private;
    std::unique_ptr<Private> d;
};

/// Convenience subclass pre-pinned to `Audio/Sink`. Designed for QML
/// `PwSinkModel { connection: PipeWireHost.connection }`. Mirrors the
/// pattern used in `phosphor-service-mpris`' `MprisPlayerModel`.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PwSinkModel : public PwNodeModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PwSinkModel)

public:
    explicit PwSinkModel(QObject* parent = nullptr);
};

/// Convenience subclass pre-pinned to `Audio/Source`.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PwSourceModel : public PwNodeModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PwSourceModel)

public:
    explicit PwSourceModel(QObject* parent = nullptr);
};

/// Convenience subclass pinned to BOTH `Stream/Output/Audio` and
/// `Stream/Input/Audio`. Streams are application-level audio endpoints
/// (Firefox's playback stream, the OBS capture stream, etc.) and the
/// mixer UI typically wants them in one list grouped by direction.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PwStreamModel : public PwNodeModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PwStreamModel)

public:
    explicit PwStreamModel(QObject* parent = nullptr);
};

} // namespace PhosphorServicePipeWire
