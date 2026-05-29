// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PwNode.h>

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
    int channelCount = 0;
    QList<qreal> volumes;
    bool muted = false;
};

PwNode::PwNode(quint32 id, QString mediaClass, PipeWireConnection* parent)
    : QObject(reinterpret_cast<QObject*>(parent))
    , d(std::make_unique<Private>())
{
    d->id = id;
    d->mediaClass = std::move(mediaClass);
}

PwNode::~PwNode() = default;

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

int PwNode::channelCount() const
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
    const QString newNick = props.value(QStringLiteral("node.nick"));
    const QString newDescription =
        props.value(QStringLiteral("node.description"), props.value(QStringLiteral("device.description")));
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
    if (d->properties != props) {
        d->properties = std::move(props);
        moved = true;
    }
    if (moved)
        Q_EMIT infoChanged();
}

void PwNode::applyProps(int channelCount, QList<qreal> volumes, bool muted)
{
    bool moved = false;
    if (d->channelCount != channelCount) {
        d->channelCount = channelCount;
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
