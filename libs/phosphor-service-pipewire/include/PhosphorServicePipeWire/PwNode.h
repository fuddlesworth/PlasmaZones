// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServicePipeWire {

class PipeWireConnection;

/// One PipeWire audio node — a sink, source, output stream, or input
/// stream. Vended by `PipeWireConnection` via `nodes()` / `nodeAdded()`
/// / `nodeRemoved()`; never directly constructed from QML
/// (registered as uncreatable so the metatype is visible to bindings
/// without inviting accidental QML-side `new PwNode()`).
///
/// All Q_PROPERTY values are populated from PipeWire `pw_node` info
/// and the SPA_PARAM_Props parameter:
/// - `id`, `name`, `mediaClass` come from the registry global event.
/// - `nick` and `description` come from the node-info properties hash
///   (PipeWire convention: `node.nick`, `node.description`).
/// - `channelCount`, `volumes`, `muted` come from a SPA_PARAM_Props
///   pod enumerated after construction and tracked via the node's
///   param-changed event.
///
/// Volumes are linear amplitudes per channel (PipeWire's storage
/// format). Consumers that want cubic / perceptual mapping for a UI
/// slider should run them through a `Mixer.Curve` helper (added in a
/// later milestone) — round-trips through the lib stay lossless.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PwNode : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PwNode)
    Q_PROPERTY(quint32 id READ id CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY infoChanged)
    Q_PROPERTY(QString nick READ nick NOTIFY infoChanged)
    Q_PROPERTY(QString description READ description NOTIFY infoChanged)
    Q_PROPERTY(QString mediaClass READ mediaClass CONSTANT)
    Q_PROPERTY(quint32 channelCount READ channelCount NOTIFY propsChanged)
    Q_PROPERTY(QList<qreal> volumes READ volumes NOTIFY propsChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY propsChanged)

public:
    ~PwNode() override;

    [[nodiscard]] quint32 id() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] QString nick() const;
    [[nodiscard]] QString description() const;
    [[nodiscard]] QString mediaClass() const;
    [[nodiscard]] quint32 channelCount() const;
    [[nodiscard]] QList<qreal> volumes() const;
    [[nodiscard]] bool muted() const;
    /// Full property hash (`node.name`, `application.name`,
    /// `media.role`, ...). Read-only; QML rarely needs more than the
    /// named accessors above but the hash is here for advanced
    /// bindings.
    [[nodiscard]] QHash<QString, QString> properties() const;

    /// Typed accessor for the owning connection. PwNode's write slots
    /// (`setVolume`, `setMuted`) route through it so the loop-thread
    /// dispatch is encapsulated.
    [[nodiscard]] PipeWireConnection* connection() const;

public Q_SLOTS:
    /// Set every channel's linear amplitude to `value`. Convenience
    /// for QML sliders that drive a single bar across the whole node.
    /// The write is asynchronous: `propsChanged` fires after the
    /// daemon echoes the new SPA_PARAM_Props pod, not immediately.
    ///
    /// Expected input range: linear amplitude, approximately
    /// `[0.0, 1.0]` for the normal mixer range, with values above
    /// `1.0` permitted for boost (PipeWire accepts them but most
    /// hardware sinks clip). Callers are responsible for clamping
    /// to their own UI's allowed range before invoking; the slot
    /// forwards `value` to the daemon verbatim.
    void setVolume(qreal value);
    /// Per-channel write. Forwards verbatim to
    /// `PipeWireConnection::writeVolumes`. PipeWire clamps the array
    /// to `SPA_AUDIO_MAX_CHANNELS` and ignores entries beyond
    /// `channelCount()`; callers should size `values` to match
    /// `channelCount()` to avoid silent truncation.
    ///
    /// Each entry follows the same linear-amplitude contract as
    /// `setVolume` (approximately `[0.0, 1.0]+`, caller-clamped).
    void setVolumes(const QList<qreal>& values);
    /// Asynchronous mute write. `propsChanged` fires once the daemon
    /// confirms.
    void setMuted(bool muted);

Q_SIGNALS:
    void infoChanged();
    void propsChanged();

private:
    friend class PipeWireConnection;

    /// Constructed by `PipeWireConnection` on the GUI thread once the
    /// registry's `global_added` event has bounced back from the loop
    /// thread. The connection becomes the QObject parent so destruction
    /// is automatic when the connection tears down. Private so only
    /// the friended connection can instantiate nodes — downstream
    /// code cannot fabricate nodes the registry never reported.
    PwNode(quint32 id, QString mediaClass, PipeWireConnection* parent);

    /// Called by `PipeWireConnection` from the GUI thread when the
    /// underlying `pw_node` reports new info (name, description,
    /// properties hash). Emits `infoChanged` only when at least one
    /// observable field actually moved.
    void applyInfo(QHash<QString, QString> props);
    /// Called by `PipeWireConnection` from the GUI thread when the
    /// underlying SPA_PARAM_Props pod refreshes. Emits `propsChanged`
    /// only when an observable field actually moved.
    void applyProps(int channelCount, QList<qreal> volumes, bool muted);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServicePipeWire
