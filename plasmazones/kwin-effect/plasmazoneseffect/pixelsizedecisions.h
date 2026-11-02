// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QSize>
#include <QString>

#include <optional>

/// Which output pixel sizes the effect owes the daemon, and when the
/// screen-shape rule verdicts drop, header-only so the repeat sequences are
/// unit-testable without a compositor (same pattern as windowscreendecisions.h).
/// The KWin reads and the setPixelSizeFromKWin calls are in screens.cpp.
namespace PlasmaZones::PixelSizeDecisions {

/// One output as KWin describes it.
struct OutputPixelSize
{
    QString connector; ///< LogicalOutput::name
    QSize pixelSize; ///< LogicalOutput::pixelSize: the current mode, already rotated
    bool placeholder = false; ///< KWin's stand-in while no physical output exists
};

/// One setPixelSizeFromKWin call. A 0x0 size is the clear.
struct Report
{
    QString connector;
    QSize pixelSize;

    bool operator==(const Report&) const = default;
};

struct FlushOutcome
{
    QList<Report> reports;
    /// A size the screen-shape rule fields read moved, so the rule verdicts
    /// drop once for the burst.
    bool shapeChanged = false;
};

struct RemovalOutcome
{
    /// The clear for the removed output's connector, when one is owed.
    std::optional<Report> report;
    /// A live output still carries the connector, so a flush re-reads it
    /// instead of a clear.
    bool flushOwed = false;
};

/// The effect's two records of each connector's size.
struct Ledger
{
    /// Each output's size as the last flush read it, so a refresh-rate-only
    /// currentModeChanged does not drop every rule verdict.
    QHash<QString, QSize> seen;
    /// The size this bridge last sent the daemon. Forgotten with the daemon
    /// (forgetSent), so the next bridge is sent every output again.
    QHash<QString, QSize> sent;

    /// The call owed for @p size on @p connector: none while the bridge is
    /// down (the size stays owed), none for a value this bridge already sent,
    /// and for an empty size the 0x0 clear, only when this bridge sent the
    /// connector a size.
    std::optional<Report> report(bool bridgeUp, const QString& connector, const QSize& size)
    {
        if (!bridgeUp || connector.isEmpty()) {
            return std::nullopt;
        }
        if (size.isEmpty()) {
            if (sent.remove(connector) == 0) {
                return std::nullopt;
            }
            return Report{connector, QSize(0, 0)};
        }
        const auto previous = sent.constFind(connector);
        if (previous != sent.constEnd() && *previous == size) {
            return std::nullopt;
        }
        sent.insert(connector, size);
        return Report{connector, size};
    }

    /// Every non-placeholder output's size is reported, whether or not it
    /// moved: a size seen while the bridge was down is still owed. Only a size
    /// that differs from the last one seen counts as a shape change, since a
    /// refresh-rate change emits currentModeChanged too and moves nothing a
    /// rule reads.
    FlushOutcome flush(bool bridgeUp, const QList<OutputPixelSize>& outputs)
    {
        FlushOutcome outcome;
        for (const OutputPixelSize& output : outputs) {
            if (output.placeholder) {
                continue;
            }
            if (const auto owed = report(bridgeUp, output.connector, output.pixelSize)) {
                outcome.reports.append(*owed);
            }
            const auto previous = seen.constFind(output.connector);
            if (previous != seen.constEnd() && *previous == output.pixelSize) {
                continue;
            }
            seen.insert(output.connector, output.pixelSize);
            outcome.shapeChanged = true;
        }
        return outcome;
    }

    /// Every non-placeholder output's size for a bridge that just came up. The
    /// seen sizes are left alone: nothing a rule reads moved.
    QList<Report> push(bool bridgeUp, const QList<OutputPixelSize>& outputs)
    {
        QList<Report> reports;
        for (const OutputPixelSize& output : outputs) {
            if (output.placeholder) {
                continue;
            }
            if (const auto owed = report(bridgeUp, output.connector, output.pixelSize)) {
                reports.append(*owed);
            }
        }
        return reports;
    }

    /// An output went away. The effect is the daemon's only writer of the
    /// size, so the connector is cleared there, unless a live output still
    /// carries it (a monitor swapped on the same port, whose add KWin emits
    /// before this removal). That one is re-read by a flush instead, so a
    /// clear cannot land after the live output's report and wipe it.
    RemovalOutcome remove(bool bridgeUp, const QString& connector, bool connectorStillLive)
    {
        seen.remove(connector);
        if (connectorStillLive) {
            return {std::nullopt, true};
        }
        return {report(bridgeUp, connector, QSize()), false};
    }

    /// The daemon went away and its sizes with it.
    void forgetSent()
    {
        sent.clear();
    }
};

} // namespace PlasmaZones::PixelSizeDecisions
