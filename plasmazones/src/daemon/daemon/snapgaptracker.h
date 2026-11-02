// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace PlasmaZones {

/**
 * @brief The snapping gap-rule verdict each screen's snapped windows were laid
 *        out under, so a verdict that moves can restate them.
 *
 * A snapped window takes its gaps when it is placed. The settings path
 * restates every snapping screen when a configured gap changes, but a gap RULE
 * verdict (LayoutRegistry::resolveContextGaps in the "snapping" mode) moves
 * with no setting changing: a rule edit, a colour-scheme flip, a monitor mode
 * change under a shape-keyed rule. This records each screen's verdict and
 * reports the screens whose verdict moved since.
 *
 * A verdict is recorded with the (desktop, activity) context it was resolved
 * in. A desktop or activity switch shows windows that were laid out in the
 * other context, so a verdict that differs ACROSS a switch is not a move: it
 * is re-recorded by @ref rebase and never reported. Only a verdict that moves
 * within one context is.
 *
 * Pure logic over an injected resolver, so the repeat sequences run in a test
 * without a Daemon.
 */
class SnapGapTracker
{
public:
    /// One screen's resolved snapping gap override and the context it was
    /// resolved in. @c gaps is the PerScreenKeys-shaped map
    /// GeometryUtils::contextGapOverrideMap builds, empty when no rule fills
    /// a gap slot.
    struct Verdict
    {
        int desktop = 0;
        QString activity;
        QVariantMap gaps;

        bool operator==(const Verdict& other) const = default;
    };

    /// Resolves a screen's verdict in its current context.
    using Resolver = std::function<Verdict(const QString& screenId)>;

    /// Bring the record in line with the screen set and contexts in
    /// @p screenIds without reporting anything. A screen with no record, or
    /// whose context differs from its record's, is recorded afresh. A screen
    /// whose context is unchanged keeps its record as it is, so a verdict that
    /// moved stays visible to the next @ref takeChanges. A screen missing from
    /// @p screenIds loses its record.
    void rebase(const QStringList& screenIds, const Resolver& resolve);

    /// Record the current verdict of every screen in @p screenIds and return
    /// the ones whose verdict moved within an unchanged context, in
    /// @p screenIds order. A screen with no record, or whose context changed,
    /// is recorded and not reported. A screen missing from @p screenIds loses
    /// its record.
    QStringList takeChanges(const QStringList& screenIds, const Resolver& resolve);

    /// Re-record @p screenIds at their current verdict, for screens whose
    /// windows were just placed under it by another path. Screens outside
    /// @p screenIds are left as they are.
    void refresh(const QStringList& screenIds, const Resolver& resolve);

    /// The recorded verdict for @p screenId, nullopt when there is none.
    std::optional<Verdict> recorded(const QString& screenId) const;

    void clear();

private:
    QHash<QString, Verdict> m_verdicts;
};

} // namespace PlasmaZones
