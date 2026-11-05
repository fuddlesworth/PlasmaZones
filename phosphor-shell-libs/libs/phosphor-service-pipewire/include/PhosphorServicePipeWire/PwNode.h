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
///
/// Post-removal slot behavior: once the underlying daemon-side
/// `pw_node` has been removed (between the daemon's `global_remove`
/// event and the GUI's `nodeRemoved` firing, plus any tail where a
/// QML binding still holds a stale pointer), `setVolume` /
/// `setVolumes` / `setMuted` route the write through the owning
/// `PipeWireConnection` to the loop thread. There, the registry
/// lookup for the (now-gone) node id misses and the write is
/// silently dropped — no error, no signal, no crash. Treat these
/// slots as best-effort once the model has signalled removal.
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
    /// @internal C++ callers only; not bindable from QML
    /// (`QHash<QString, QString>` has no QML metatype, and the accessor
    /// is intentionally neither Q_PROPERTY nor Q_INVOKABLE).
    ///
    /// Full property hash (`node.name`, `application.name`,
    /// `media.role`, ...). QML should rely on the named accessors above
    /// (`name`, `nick`, `description`, ...) for individual fields.
    /// Lives in this block (above the QML-visible Q_PROPERTY getters)
    /// rather than below them because it's grouped with the other
    /// info-derived accessors it parallels — `name` / `nick` /
    /// `description` are all surfaces of the same property hash.
    [[nodiscard]] QHash<QString, QString> properties() const;

    /// Typed accessor for the owning connection. PwNode's write slots
    /// (`setVolume`, `setVolumes`, `setMuted`) route through it so the
    /// loop-thread dispatch is encapsulated.
    [[nodiscard]] PipeWireConnection* connection() const;

    /// @internal Called by `PipeWireConnection`'s loop→GUI thread bounce
    /// when the underlying `pw_node` reports new info (name, description,
    /// properties hash). Emits `infoChanged` only when at least one
    /// observable field actually moved. NOT for general use — this is
    /// public solely because `PipeWireConnection::Private` (a nested
    /// implementation class) needs to invoke it, and granting nested-
    /// friend access portably is brittler than documenting the
    /// callability contract here. Callers other than
    /// `PipeWireConnection`'s loop handlers should treat this method as
    /// non-existent.
    void applyInfo(QHash<QString, QString> props);
    /// @internal Called by `PipeWireConnection`'s loop→GUI thread bounce
    /// when the underlying SPA_PARAM_Props pod refreshes. Emits
    /// `propsChanged` only when an observable field actually moved.
    /// Same "internal API" caveat as `applyInfo` — public only because
    /// `PipeWireConnection::Private` needs to call it.
    void applyProps(int channelCount, QList<qreal> volumes, bool muted);

public Q_SLOTS:
    /// Set every channel's linear amplitude to `value`. Convenience
    /// for QML sliders that drive a single bar across the whole node.
    /// The write is asynchronous: `propsChanged` fires after the
    /// daemon echoes the new SPA_PARAM_Props pod, not immediately.
    ///
    /// Expected input range: linear amplitude, approximately
    /// `[0.0, 1.0]` for the normal mixer range, with values above
    /// `1.0` permitted for boost (PipeWire accepts them but most
    /// hardware sinks clip). Negative values are not part of the
    /// linear-amplitude contract: PipeWire may interpret them as
    /// phase-inverted (driver-dependent) or clamp them to zero,
    /// neither of which is a useful UI behaviour. Callers should
    /// clamp to `[0.0, expected upper bound]` at the UI layer before
    /// invoking; the slot forwards `value` to the daemon verbatim.
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
    ///
    /// `parent` is REQUIRED (asserted non-null in the ctor body): the
    /// connection-as-parent relationship is the node's only lifetime
    /// anchor. The `connection()` accessor downcasts `parent()` to a
    /// `PipeWireConnection*` and `setVolumes` / `setMuted` route writes
    /// through it; both would dereference null without the parent.
    PwNode(quint32 id, QString mediaClass, PipeWireConnection* parent);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServicePipeWire
