// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorGapsModel.h"

#include "EditorController.h"
#include "undo/UndoController.h"
#include "undo/commands/UpdateGapOverrideCommand.h"

#include "../config/configdefaults.h"
#include "core/types/constants.h"

#include "phosphor_i18n.h"

#include <PhosphorZones/ZoneJsonKeys.h>

namespace PlasmaZones {

EditorGapsModel::EditorGapsModel(EditorController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_cachedGlobalZonePadding(PlasmaZones::Defaults::InnerGap)
    , m_cachedGlobalOuterGap(PlasmaZones::Defaults::OuterGap)
    , m_cachedGlobalOuterGapTop(PlasmaZones::Defaults::OuterGap)
    , m_cachedGlobalOuterGapBottom(PlasmaZones::Defaults::OuterGap)
    , m_cachedGlobalOuterGapLeft(PlasmaZones::Defaults::OuterGap)
    , m_cachedGlobalOuterGapRight(PlasmaZones::Defaults::OuterGap)
{
}

int EditorGapsModel::zonePadding() const
{
    return m_zonePadding;
}

int EditorGapsModel::outerGap() const
{
    return m_outerGap;
}

bool EditorGapsModel::hasZonePaddingOverride() const
{
    return m_zonePadding >= 0;
}

bool EditorGapsModel::hasOuterGapOverride() const
{
    return m_outerGap >= 0;
}

int EditorGapsModel::globalZonePadding() const
{
    return m_cachedGlobalZonePadding;
}

int EditorGapsModel::globalOuterGap() const
{
    return m_cachedGlobalOuterGap;
}

void EditorGapsModel::setZonePadding(int padding)
{
    if (padding < -1) {
        padding = -1;
    }
    if (m_zonePadding != padding) {
        auto* cmd = new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::ZonePadding,
                                                 m_zonePadding, padding);
        m_controller->undoController()->push(cmd);
    }
}

void EditorGapsModel::setZonePaddingDirect(int padding)
{
    if (padding < -1) {
        padding = -1;
    }
    if (m_zonePadding != padding) {
        m_zonePadding = padding;
        m_controller->markUnsaved();
        Q_EMIT zonePaddingChanged();
    }
}

void EditorGapsModel::setOuterGap(int gap)
{
    if (gap < -1) {
        gap = -1;
    }
    if (m_outerGap != gap) {
        auto* cmd =
            new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGap, m_outerGap, gap);
        m_controller->undoController()->push(cmd);
    }
}

void EditorGapsModel::setOuterGapDirect(int gap)
{
    if (gap < -1) {
        gap = -1;
    }
    if (m_outerGap != gap) {
        m_outerGap = gap;
        m_controller->markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

bool EditorGapsModel::usePerSideOuterGap() const
{
    return m_usePerSideOuterGap;
}

int EditorGapsModel::outerGapTop() const
{
    return m_outerGapTop;
}

int EditorGapsModel::outerGapBottom() const
{
    return m_outerGapBottom;
}

int EditorGapsModel::outerGapLeft() const
{
    return m_outerGapLeft;
}

int EditorGapsModel::outerGapRight() const
{
    return m_outerGapRight;
}

bool EditorGapsModel::globalUsePerSideOuterGap() const
{
    return m_cachedGlobalUsePerSideOuterGap;
}

int EditorGapsModel::globalOuterGapTop() const
{
    return m_cachedGlobalOuterGapTop;
}

int EditorGapsModel::globalOuterGapBottom() const
{
    return m_cachedGlobalOuterGapBottom;
}

int EditorGapsModel::globalOuterGapLeft() const
{
    return m_cachedGlobalOuterGapLeft;
}

int EditorGapsModel::globalOuterGapRight() const
{
    return m_cachedGlobalOuterGapRight;
}

void EditorGapsModel::setUsePerSideOuterGap(bool enabled)
{
    if (m_usePerSideOuterGap != enabled) {
        // Use a gap override command for undo (toggling per-side is conceptually a gap change)
        auto* cmd = new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::UsePerSideOuterGap,
                                                 m_usePerSideOuterGap ? 1 : 0, enabled ? 1 : 0);
        m_controller->undoController()->push(cmd);
    }
}

void EditorGapsModel::setUsePerSideOuterGapDirect(bool enabled)
{
    if (m_usePerSideOuterGap != enabled) {
        m_usePerSideOuterGap = enabled;
        m_controller->markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorGapsModel::setOuterGapTop(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapTop != gap) {
        auto* cmd = new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGapTop,
                                                 m_outerGapTop, gap);
        m_controller->undoController()->push(cmd);
    }
}

void EditorGapsModel::setOuterGapTopDirect(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapTop != gap) {
        m_outerGapTop = gap;
        m_controller->markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorGapsModel::setOuterGapBottom(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapBottom != gap) {
        auto* cmd = new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGapBottom,
                                                 m_outerGapBottom, gap);
        m_controller->undoController()->push(cmd);
    }
}

void EditorGapsModel::setOuterGapBottomDirect(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapBottom != gap) {
        m_outerGapBottom = gap;
        m_controller->markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorGapsModel::setOuterGapLeft(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapLeft != gap) {
        auto* cmd = new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGapLeft,
                                                 m_outerGapLeft, gap);
        m_controller->undoController()->push(cmd);
    }
}

void EditorGapsModel::setOuterGapLeftDirect(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapLeft != gap) {
        m_outerGapLeft = gap;
        m_controller->markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorGapsModel::setOuterGapRight(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapRight != gap) {
        auto* cmd = new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGapRight,
                                                 m_outerGapRight, gap);
        m_controller->undoController()->push(cmd);
    }
}

void EditorGapsModel::setOuterGapRightDirect(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapRight != gap) {
        m_outerGapRight = gap;
        m_controller->markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorGapsModel::clearZonePaddingOverride()
{
    setZonePadding(-1);
}

void EditorGapsModel::clearOuterGapOverride()
{
    // Early return if nothing to clear — avoids empty macro on undo stack
    bool hasAnyOverride = m_outerGap != -1 || m_usePerSideOuterGap || m_outerGapTop != -1 || m_outerGapBottom != -1
        || m_outerGapLeft != -1 || m_outerGapRight != -1;
    if (!hasAnyOverride) {
        return;
    }

    UndoController* undo = m_controller->undoController();

    // Snapshot current state for undo, then push a macro command that resets all gap overrides.
    // Uses beginMacro/endMacro so the entire clear is one undo step.
    undo->beginMacro(PhosphorI18n::tr("Clear Edge Gap Override", "@action"));
    if (m_outerGap != -1) {
        undo->push(
            new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGap, m_outerGap, -1));
    }
    if (m_usePerSideOuterGap) {
        undo->push(
            new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::UsePerSideOuterGap, 1, 0));
    }
    if (m_outerGapTop != -1) {
        undo->push(new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGapTop,
                                                m_outerGapTop, -1));
    }
    if (m_outerGapBottom != -1) {
        undo->push(new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGapBottom,
                                                m_outerGapBottom, -1));
    }
    if (m_outerGapLeft != -1) {
        undo->push(new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGapLeft,
                                                m_outerGapLeft, -1));
    }
    if (m_outerGapRight != -1) {
        undo->push(new UpdateGapOverrideCommand(m_controller, UpdateGapOverrideCommand::GapType::OuterGapRight,
                                                m_outerGapRight, -1));
    }
    undo->endMacro();
}

void EditorGapsModel::resetOverrides()
{
    m_zonePadding = -1;
    m_outerGap = -1;
    m_usePerSideOuterGap = false;
    m_outerGapTop = -1;
    m_outerGapBottom = -1;
    m_outerGapLeft = -1;
    m_outerGapRight = -1;
}

void EditorGapsModel::emitOverrideSignals()
{
    Q_EMIT zonePaddingChanged();
    Q_EMIT outerGapChanged();
}

void EditorGapsModel::loadFromJson(const QJsonObject& layoutObj)
{
    const int oldZonePadding = m_zonePadding;

    m_zonePadding = layoutObj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::ZonePadding))
        ? layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZonePadding)].toInt(-1)
        : -1;
    m_outerGap = layoutObj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGap))
        ? layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGap)].toInt(-1)
        : -1;
    m_usePerSideOuterGap = layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::UsePerSideOuterGap)].toBool(false);
    m_outerGapTop = layoutObj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapTop))
        ? layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapTop)].toInt(-1)
        : -1;
    m_outerGapBottom = layoutObj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapBottom))
        ? layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapBottom)].toInt(-1)
        : -1;
    m_outerGapLeft = layoutObj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapLeft))
        ? layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapLeft)].toInt(-1)
        : -1;
    m_outerGapRight = layoutObj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapRight))
        ? layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapRight)].toInt(-1)
        : -1;

    if (m_zonePadding != oldZonePadding) {
        Q_EMIT zonePaddingChanged();
    }
    // Intentional policy exception to signal-on-change rule:
    // always emit outerGapChanged here because per-side gap values (top/bottom/left/right)
    // can differ between layouts even when m_outerGap is numerically unchanged.
    Q_EMIT outerGapChanged();
}

void EditorGapsModel::writeToJson(QJsonObject& layoutObj) const
{
    // Include per-layout gap overrides (only if set, >= 0)
    if (m_zonePadding >= 0) {
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZonePadding)] = m_zonePadding;
    }
    if (m_outerGap >= 0) {
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGap)] = m_outerGap;
    }
    // Serialize per-side toggle whenever enabled so user intent is preserved across save/load
    if (m_usePerSideOuterGap) {
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::UsePerSideOuterGap)] = true;
        if (m_outerGapTop >= 0)
            layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapTop)] = m_outerGapTop;
        if (m_outerGapBottom >= 0)
            layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapBottom)] = m_outerGapBottom;
        if (m_outerGapLeft >= 0)
            layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapLeft)] = m_outerGapLeft;
        if (m_outerGapRight >= 0)
            layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OuterGapRight)] = m_outerGapRight;
    }
}

void EditorGapsModel::applyGlobalSettings(const QVariantMap& values)
{
    // Read an int from the batched settings reply. Uses toInt(&ok) so a
    // malformed daemon reply (wrong type, not convertible) falls back to the
    // default rather than silently coercing to 0. Negative values are also
    // treated as invalid — these keys are all non-negative pixel counts.
    // Keys missing from the batch also fall through to the fallback.
    auto readInt = [&](const QString& key, int fallback) {
        auto it = values.constFind(key);
        if (it == values.constEnd()) {
            return fallback;
        }
        bool ok = false;
        const int v = it.value().toInt(&ok);
        if (!ok || v < 0) {
            return fallback;
        }
        return v;
    };
    auto readBool = [&](const QString& key, bool fallback) {
        auto it = values.constFind(key);
        return it == values.constEnd() ? fallback : it.value().toBool();
    };

    // innerGap
    {
        const int newValue = readInt(QStringLiteral("innerGap"), Defaults::InnerGap);
        if (m_cachedGlobalZonePadding != newValue) {
            m_cachedGlobalZonePadding = newValue;
            Q_EMIT globalZonePaddingChanged();
        }
    }

    // outerGap cluster — one aggregate signal covers any field changing.
    {
        const int newValue = readInt(QStringLiteral("outerGap"), Defaults::OuterGap);
        const bool newUsePerSide = readBool(QStringLiteral("usePerSideOuterGap"), false);
        const int newTop = readInt(QStringLiteral("outerGapTop"), Defaults::OuterGap);
        const int newBottom = readInt(QStringLiteral("outerGapBottom"), Defaults::OuterGap);
        const int newLeft = readInt(QStringLiteral("outerGapLeft"), Defaults::OuterGap);
        const int newRight = readInt(QStringLiteral("outerGapRight"), Defaults::OuterGap);

        const bool changed = (m_cachedGlobalOuterGap != newValue) || (m_cachedGlobalUsePerSideOuterGap != newUsePerSide)
            || (m_cachedGlobalOuterGapTop != newTop) || (m_cachedGlobalOuterGapBottom != newBottom)
            || (m_cachedGlobalOuterGapLeft != newLeft) || (m_cachedGlobalOuterGapRight != newRight);

        m_cachedGlobalOuterGap = newValue;
        m_cachedGlobalUsePerSideOuterGap = newUsePerSide;
        m_cachedGlobalOuterGapTop = newTop;
        m_cachedGlobalOuterGapBottom = newBottom;
        m_cachedGlobalOuterGapLeft = newLeft;
        m_cachedGlobalOuterGapRight = newRight;

        if (changed) {
            Q_EMIT globalOuterGapChanged();
        }
    }
}

} // namespace PlasmaZones
