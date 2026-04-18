// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Profile.h>

#include <PhosphorAnimation/CurveRegistry.h>

#include <QJsonValue>

namespace PhosphorAnimation {

QJsonObject Profile::toJson() const
{
    QJsonObject obj;
    if (curve) {
        obj.insert(QLatin1String("curve"), curve->toString());
    }
    obj.insert(QLatin1String("duration"), duration);
    obj.insert(QLatin1String("minDistance"), minDistance);
    obj.insert(QLatin1String("sequenceMode"), sequenceMode);
    obj.insert(QLatin1String("staggerInterval"), staggerInterval);
    if (!presetName.isEmpty()) {
        obj.insert(QLatin1String("presetName"), presetName);
    }
    return obj;
}

Profile Profile::fromJson(const QJsonObject& obj)
{
    Profile p;

    if (obj.contains(QLatin1String("curve"))) {
        const QString spec = obj.value(QLatin1String("curve")).toString();
        if (!spec.isEmpty()) {
            p.curve = CurveRegistry::instance().create(spec);
        }
    }

    if (obj.contains(QLatin1String("duration"))) {
        p.duration = obj.value(QLatin1String("duration")).toDouble(150.0);
    }
    if (obj.contains(QLatin1String("minDistance"))) {
        p.minDistance = obj.value(QLatin1String("minDistance")).toInt(0);
    }
    if (obj.contains(QLatin1String("sequenceMode"))) {
        p.sequenceMode = obj.value(QLatin1String("sequenceMode")).toInt(0);
    }
    if (obj.contains(QLatin1String("staggerInterval"))) {
        p.staggerInterval = obj.value(QLatin1String("staggerInterval")).toInt(30);
    }
    if (obj.contains(QLatin1String("presetName"))) {
        p.presetName = obj.value(QLatin1String("presetName")).toString();
    }

    return p;
}

bool Profile::operator==(const Profile& other) const
{
    // Curve equality via the virtual equals() — accounts for polymorphism.
    // Either both null or both non-null and equal.
    const bool curvesEqual =
        (curve.get() == other.curve.get()) || (curve && other.curve && curve->equals(*other.curve));
    if (!curvesEqual) {
        return false;
    }
    return qFuzzyCompare(1.0 + duration, 1.0 + other.duration) && minDistance == other.minDistance
        && sequenceMode == other.sequenceMode && staggerInterval == other.staggerInterval
        && presetName == other.presetName;
}

} // namespace PhosphorAnimation
