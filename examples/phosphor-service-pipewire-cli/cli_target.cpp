// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli_target.h"

#include "cli_io.h"

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

#include <QChar>
#include <QList>
#include <QTextStream>

namespace PhosphorPipeWireCli {

PhosphorServicePipeWire::PwNode* findNode(PhosphorServicePipeWire::PipeWireConnection& conn, uint id)
{
    for (auto* node : conn.nodes()) {
        if (node && node->id() == id)
            return node;
    }
    return nullptr;
}

PhosphorServicePipeWire::PwNode* resolveTarget(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& spec)
{
    // Reject any input containing internal whitespace: " 55" would
    // otherwise toUInt() to 55 silently, and "alsa output" is never a
    // valid node.name anyway. We accept leading/trailing whitespace by
    // trimming first. Iterating QChar walks UTF-16 code units, so
    // per-char QChar::isSpace() covers the full BMP Unicode whitespace
    // set (NBSP, U+2028 line separator, etc.); supplementary-plane
    // whitespace codepoints (none currently assigned in Unicode) would
    // not be detected, but a narrow space/tab-only check would silently
    // let the BMP cases through.
    const QString trimmed = spec.trimmed();
    for (const QChar ch : trimmed) {
        if (ch.isSpace()) {
            err() << "target '" << spec << "' contains whitespace\n";
            return nullptr;
        }
    }
    bool isNumeric = false;
    const uint id = trimmed.toUInt(&isNumeric);
    if (isNumeric)
        return findNode(conn, id);
    QString name = trimmed;
    if (trimmed == QLatin1String("default.audio.sink")) {
        name = conn.defaultSinkName();
        if (name.isEmpty()) {
            err() << "no node matches default sentinel (defaultSinkName empty)\n";
            return nullptr;
        }
    } else if (trimmed == QLatin1String("default.audio.source")) {
        name = conn.defaultSourceName();
        if (name.isEmpty()) {
            err() << "no node matches default sentinel (defaultSourceName empty)\n";
            return nullptr;
        }
    }
    if (name.isEmpty())
        return nullptr;
    for (auto* node : conn.nodes()) {
        if (node && node->name() == name)
            return node;
    }
    return nullptr;
}

QString resolveTargetToName(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& spec)
{
    auto* node = resolveTarget(conn, spec);
    if (!node) {
        err() << "no node matches target '" << spec << "'\n";
        return {};
    }
    return node->name();
}

QString labelFor(PhosphorServicePipeWire::PwNode* node)
{
    if (!node)
        return {};
    const QString nick = node->nick();
    if (!nick.isEmpty())
        return nick;
    const QString desc = node->description();
    if (!desc.isEmpty())
        return desc;
    return node->name();
}

void printNode(PhosphorServicePipeWire::PwNode* node)
{
    if (!node)
        return;
    out() << "  " << node->id() << "  " << labelFor(node) << "\n"
          << "      name:        " << node->name() << "\n"
          << "      mediaClass:  " << node->mediaClass() << "\n"
          << "      channels:    " << node->channelCount() << "\n";
    const auto vols = node->volumes();
    if (!vols.isEmpty()) {
        out() << "      volumes:    [";
        for (int i = 0; i < vols.size(); ++i) {
            if (i > 0)
                out() << ", ";
            out() << QString::number(vols.at(i), 'f', 3);
        }
        out() << "]\n";
    }
    out() << "      muted:       " << (node->muted() ? "yes" : "no") << "\n";
    // Flush after each node so pipe-truncated consumers (e.g. `cli list
    // sinks | head -5`) see complete, deterministic node blocks instead
    // of a partial last record when the downstream reader closes early.
    // QTextStream over stdout buffers by default; without the flush the
    // buffered tail can be discarded on SIGPIPE / pipe close.
    out().flush();
}

bool isAudioMediaClass(const QString& mc)
{
    for (const char* cls : kAudioMediaClasses) {
        if (mc == QLatin1String(cls))
            return true;
    }
    return false;
}

bool isKnownListKind(const QString& kind)
{
    return kind == QLatin1String("sinks") || kind == QLatin1String("sources") || kind == QLatin1String("streams");
}

} // namespace PhosphorPipeWireCli
