// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Profile.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>

#include <QJsonValue>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <cmath>
#include <limits>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcProfile, "phosphoranimation.profile")

// Rate-limit the "unknown sequenceMode" warning to one message per distinct
// raw value per process. Profiles are re-deserialised on every config
// reload; without this guard a malformed settings file (or a newer client
// writing an unknown enumerator) produces N warnings per reload, spamming
// long-lived daemons. We still want the warning on first sight so schema
// drift is visible.
bool shouldWarnUnknownSequenceMode(int raw)
{
    static QMutex mutex;
    static QSet<int> seen;
    QMutexLocker lock(&mutex);
    if (seen.contains(raw)) {
        return false;
    }
    seen.insert(raw);
    return true;
}

// Upper bound for a sane animation duration. A QQuickPropertyAnimation
// takes `int ms`; anything more than an hour is clearly malformed JSON
// and the downstream qRound() into int would otherwise risk overflow.
constexpr qreal kMaxDurationMs = 60.0 * 60.0 * 1000.0; // 1 hour
}

Profile Profile::withDefaults() const
{
    Profile out = *this;
    if (!out.curve) {
        // Match `AnimatedValue::defaultFallbackCurve()` and every other
        // "no curve set" fallback across the library: a default-
        // constructed Easing is OutCubic (0.33, 1.00, 0.68, 1.00).
        // Filling this here means `withDefaults()` is now "every field
        // has a concrete value" — matching the method name — rather
        // than the previous "all scalars filled, curve silently still
        // null" shape that surprised callers reading the header and
        // expecting a fully-populated Profile.
        out.curve = std::make_shared<const Easing>();
    }
    if (!out.duration) {
        out.duration = DefaultDuration;
    }
    if (!out.minDistance) {
        out.minDistance = DefaultMinDistance;
    }
    if (!out.sequenceMode) {
        out.sequenceMode = DefaultSequenceMode;
    }
    if (!out.staggerInterval) {
        out.staggerInterval = DefaultStaggerInterval;
    }
    return out;
}

QJsonObject Profile::toJson() const
{
    QJsonObject obj;
    if (curve) {
        obj.insert(QLatin1String(JsonFieldCurve), curve->toString());
    }
    if (duration) {
        obj.insert(QLatin1String(JsonFieldDuration), *duration);
    }
    if (minDistance) {
        obj.insert(QLatin1String(JsonFieldMinDistance), *minDistance);
    }
    if (sequenceMode) {
        obj.insert(QLatin1String(JsonFieldSequenceMode), static_cast<int>(*sequenceMode));
    }
    if (staggerInterval) {
        obj.insert(QLatin1String(JsonFieldStaggerInterval), *staggerInterval);
    }
    if (presetName) {
        obj.insert(QLatin1String(JsonFieldPresetName), *presetName);
    }
    return obj;
}

Profile Profile::fromJson(const QJsonObject& obj, const CurveRegistry& registry)
{
    Profile p;

    if (obj.contains(QLatin1String(JsonFieldCurve))) {
        const QString spec = obj.value(QLatin1String(JsonFieldCurve)).toString();
        if (!spec.isEmpty()) {
            // Route through tryCreate (not create): a malformed spec
            // would otherwise silently fall through to the library
            // default cubic-bezier inside create(), masking user typos
            // like "srping:14,0.6" as "my curve works but feels wrong".
            // When resolution fails, leave p.curve unset (effectiveXxx
            // will still substitute the library default) and warn loudly
            // with the original spec so the author can find the typo.
            p.curve = registry.tryCreate(spec);
            if (!p.curve) {
                qCWarning(lcProfile).nospace() << "Profile::fromJson: curve spec '" << spec
                                               << "' did not resolve — keeping profile without a curve "
                                                  "(library default will apply at animation time)";
            }
        }
    }

    if (obj.contains(QLatin1String(JsonFieldDuration))) {
        const qreal raw = obj.value(QLatin1String(JsonFieldDuration)).toDouble(DefaultDuration);
        // Reject NaN / infinity / non-positive / absurdly-large values.
        // The downstream qRound() into int and QQuickPropertyAnimation::
        // setDuration(int) have no tolerance for these — NaN rounds to
        // UB, negatives are silently treated as 0, and large doubles
        // overflow the int conversion. Leaving the field unset makes
        // `effectiveDuration()` substitute the library default, which
        // is the correct fallback for garbage input.
        if (!std::isfinite(raw) || raw <= 0.0 || raw > kMaxDurationMs) {
            qCWarning(lcProfile).nospace()
                << "Profile::fromJson: rejecting duration " << raw << " (expected 0 < duration <= " << kMaxDurationMs
                << " ms) — library default will apply";
        } else {
            p.duration = raw;
        }
    }
    if (obj.contains(QLatin1String(JsonFieldMinDistance))) {
        const int raw = obj.value(QLatin1String(JsonFieldMinDistance)).toInt(DefaultMinDistance);
        // Negative minDistance would make the distance-skip check
        // trivially true for every animation (no real distance is
        // less than a negative threshold), effectively disabling the
        // skip everywhere. Zero is the documented "no skip" value
        // and is accepted.
        if (raw < 0) {
            qCWarning(lcProfile).nospace()
                << "Profile::fromJson: rejecting negative minDistance " << raw << " — library default will apply";
        } else {
            p.minDistance = raw;
        }
    }
    if (obj.contains(QLatin1String(JsonFieldSequenceMode))) {
        const int raw = obj.value(QLatin1String(JsonFieldSequenceMode)).toInt(static_cast<int>(DefaultSequenceMode));
        // Map valid enumerators; anything else falls back to the library
        // default. This is NOT forward-compat with future enumerators
        // written by a newer client — those would silently land on
        // AllAtOnce, not on a behaviorally-similar mode. If new modes
        // are added, bump the schema and route through migration code.
        if (raw == static_cast<int>(SequenceMode::AllAtOnce) || raw == static_cast<int>(SequenceMode::Cascade)) {
            p.sequenceMode = static_cast<SequenceMode>(raw);
        } else {
            // Log so schema drift doesn't silently paper over as "AllAtOnce"
            // — a newer client writing an unknown enumerator is something a
            // future-maintainer wants to see in logs, not discover via a
            // mysterious animation-behaviour regression. Rate-limited to
            // one message per distinct value per process (see
            // shouldWarnUnknownSequenceMode).
            if (shouldWarnUnknownSequenceMode(raw)) {
                qCWarning(lcProfile) << "Profile::fromJson: unknown sequenceMode" << raw
                                     << "— substituting DefaultSequenceMode (schema drift?)";
            }
            p.sequenceMode = DefaultSequenceMode;
        }
    }
    if (obj.contains(QLatin1String(JsonFieldStaggerInterval))) {
        p.staggerInterval = obj.value(QLatin1String(JsonFieldStaggerInterval)).toInt(DefaultStaggerInterval);
    }
    if (obj.contains(QLatin1String(JsonFieldPresetName))) {
        p.presetName = obj.value(QLatin1String(JsonFieldPresetName)).toString();
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
    return duration == other.duration && minDistance == other.minDistance && sequenceMode == other.sequenceMode
        && staggerInterval == other.staggerInterval && presetName == other.presetName;
}

} // namespace PhosphorAnimation
