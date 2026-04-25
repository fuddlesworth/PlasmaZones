// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QString>
#include <QVariantMap>

class QObject;

namespace PhosphorEngineApi {

class IAutotileSettings
{
public:
    virtual ~IAutotileSettings() = default;

    virtual QObject* asQObject() = 0;

    virtual QString defaultAutotileAlgorithm() const = 0;
    virtual void setDefaultAutotileAlgorithm(const QString& algorithm) = 0;

    virtual qreal autotileSplitRatio() const = 0;
    virtual void setAutotileSplitRatio(qreal ratio) = 0;

    virtual qreal autotileSplitRatioStep() const = 0;

    virtual int autotileMasterCount() const = 0;
    virtual void setAutotileMasterCount(int count) = 0;

    virtual int autotileInnerGap() const = 0;
    virtual int autotileOuterGap() const = 0;
    virtual bool autotileUsePerSideOuterGap() const = 0;
    virtual int autotileOuterGapTop() const = 0;
    virtual int autotileOuterGapBottom() const = 0;
    virtual int autotileOuterGapLeft() const = 0;
    virtual int autotileOuterGapRight() const = 0;

    virtual bool autotileFocusNewWindows() const = 0;
    virtual bool autotileSmartGaps() const = 0;
    virtual bool autotileFocusFollowsMouse() const = 0;
    virtual bool autotileRespectMinimumSize() const = 0;

    virtual int autotileMaxWindows() const = 0;
    virtual void setAutotileMaxWindows(int max) = 0;

    virtual int autotileInsertPositionInt() const = 0;
    virtual int autotileOverflowBehavior() const = 0;

    virtual QVariantMap autotilePerAlgorithmSettings() const = 0;
    virtual void setAutotilePerAlgorithmSettings(const QVariantMap& settings) = 0;

    virtual void save() = 0;
};

} // namespace PhosphorEngineApi
