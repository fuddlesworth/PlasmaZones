// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PwNode.h>

#include <PhosphorServicePipeWire/PipeWireConnection.h>

namespace PhosphorServicePipeWire {

class PwNode::Private
{
public:
    quint32 id = 0;
    QString name;
    QString nick;
    QString description;
    QString mediaClass;
    QHash<QString, QString> properties;
    quint32 channelCount = 0;
    QList<qreal> volumes;
    bool muted = false;
};

PwNode::PwNode(quint32 id, QString mediaClass, PipeWireConnection* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    // Connection-as-parent contract: PwNode lifetime is tied to the
    // owning PipeWireConnection via Qt's parent-child machinery (see
    // header doc on the ctor). The only legal caller is
    // PipeWireConnection::Private's loop→GUI bounce, which always
    // hands `this` as the parent — a null parent would orphan the
    // node and let it outlive the connection that vended it. The
    // assertion makes a future caller that drops the contract crash
    // loudly in debug builds; in release it compiles out, and the
    // write slots' `connection()` guards (they no-op when parent() is
    // not a PipeWireConnection) keep a wrongly-parented node inert
    // rather than crashing.
    Q_ASSERT(parent);
    d->id = id;
    d->mediaClass = std::move(mediaClass);
}

PwNode::~PwNode() = default;

PipeWireConnection* PwNode::connection() const
{
    return qobject_cast<PipeWireConnection*>(parent());
}

quint32 PwNode::id() const
{
    return d->id;
}

QString PwNode::name() const
{
    return d->name;
}

QString PwNode::nick() const
{
    return d->nick;
}

QString PwNode::description() const
{
    return d->description;
}

QString PwNode::mediaClass() const
{
    return d->mediaClass;
}

quint32 PwNode::channelCount() const
{
    return d->channelCount;
}

QList<qreal> PwNode::volumes() const
{
    return d->volumes;
}

bool PwNode::muted() const
{
    return d->muted;
}

QHash<QString, QString> PwNode::properties() const
{
    return d->properties;
}

void PwNode::applyInfo(QHash<QString, QString> props)
{
    // Per-PipeWire convention: `node.name` is the canonical name,
    // `node.nick` the short user-facing label, `node.description` the
    // full one. Fall back through PulseAudio-style keys
    // (`device.description`) when the PW-native key is absent so
    // upstream apps that haven't migrated still get a usable label.
    const QString newName = props.value(QStringLiteral("node.name"));
    // No Pulse-equivalent fallback for nick — PipeWire-native field;
    // PulseAudio surfaces a short user-facing label via
    // `device.description`, which we already use as the description
    // fallback, and there's no separate "nick" concept on the Pulse
    // side to map back through. Leaving nick empty lets the display-
    // role fallback (nick → description → name) pick up the Pulse
    // description correctly.
    const QString newNick = props.value(QStringLiteral("node.nick"));
    // Compute the fallback lazily: the two-argument QHash::value form
    // evaluates the default argument unconditionally (C++ function-arg
    // eval rules), so the `device.description` lookup would always
    // run even when `node.description` is present. The explicit
    // isEmpty check skips the second hash probe on the common path.
    QString newDescription = props.value(QStringLiteral("node.description"));
    if (newDescription.isEmpty())
        newDescription = props.value(QStringLiteral("device.description"));
    bool moved = false;
    if (d->name != newName) {
        d->name = newName;
        moved = true;
    }
    if (d->nick != newNick) {
        d->nick = newNick;
        moved = true;
    }
    if (d->description != newDescription) {
        d->description = newDescription;
        moved = true;
    }
    // Any diff in the full property hash flips `moved`, even for keys
    // we don't surface as named accessors. That's intentional: the
    // raw hash is observable through `properties()`, so a QML binding
    // like `node.properties["application.name"]` would otherwise miss
    // changes. The cost is occasional spurious infoChanged emissions
    // when only an unsurfaced key moved — acceptable: PipeWire
    // info-event cadence is low enough (one per pw_node info update)
    // that this isn't a hot path.
    if (d->properties != props) {
        d->properties = std::move(props);
        moved = true;
    }
    if (moved)
        Q_EMIT infoChanged();
}

void PwNode::setVolume(qreal value)
{
    auto* conn = connection();
    if (!conn)
        return;
    // Promote the scalar to a per-channel array using the current
    // channel count if known; if the node hasn't published a Props pod
    // yet, fall back to a single-channel write so the request still
    // has effect.
    const quint32 count = d->channelCount > 0 ? d->channelCount : 1u;
    QList<qreal> values;
    values.reserve(static_cast<qsizetype>(count));
    for (quint32 i = 0; i < count; ++i) {
        values.append(value);
    }
    conn->writeVolumes(d->id, values);
}

void PwNode::setVolumes(const QList<qreal>& values)
{
    auto* conn = connection();
    if (!conn)
        return;
    conn->writeVolumes(d->id, values);
}

void PwNode::setMuted(bool muted)
{
    auto* conn = connection();
    if (!conn)
        return;
    conn->writeMuted(d->id, muted);
}

void PwNode::applyProps(int channelCount, QList<qreal> volumes, bool muted)
{
    bool moved = false;
    // applyProps takes `int` (PipeWire pod traversal yields signed
    // counts) but the surfaced property is `quint32`. Clamp negative
    // values to zero so the cast never aliases as a huge positive
    // count: PipeWire shouldn't deliver negatives here, but a
    // defensive clamp is cheaper than tracking down a phantom
    // multi-billion-channel node downstream. The Q_ASSERT below
    // catches the wiring-bug case in debug builds; the clamp keeps
    // release builds well-behaved if the assertion ever evolves to
    // accept negatives.
    const quint32 newChannelCount = channelCount < 0 ? 0u : static_cast<quint32>(channelCount);
    // Contract: caller (always pipewireconnection.cpp's onNodeParam path)
    // derives both `channelCount` and `volumes` from the SAME pod
    // traversal, so size mismatch here implies a wiring bug on the
    // producer side. A QML binding that does `Repeater { model:
    // node.channelCount }` and indexes `node.volumes[i]` would OOB on
    // mismatch — make the violation loud in debug.
    Q_ASSERT(static_cast<quint32>(volumes.size()) == newChannelCount);
    // Release builds must stay OOB-safe even when the assertion above
    // compiles out: never publish a channelCount larger than the volume
    // array consumers actually index, or `volumes[i]` for i in
    // [0, channelCount) reads past the end. Clamp to the real array size
    // so the two properties consumers observe can never desync into an
    // out-of-bounds shape; a too-small published count degrades to
    // showing fewer channels, which is safe.
    const quint32 safeChannelCount = qMin(newChannelCount, static_cast<quint32>(volumes.size()));
    if (d->channelCount != safeChannelCount) {
        d->channelCount = safeChannelCount;
        moved = true;
    }
    if (d->volumes != volumes) {
        d->volumes = std::move(volumes);
        moved = true;
    }
    if (d->muted != muted) {
        d->muted = muted;
        moved = true;
    }
    if (moved)
        Q_EMIT propsChanged();
}

} // namespace PhosphorServicePipeWire
