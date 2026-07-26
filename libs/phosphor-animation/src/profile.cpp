// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Profile.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>

#include <QJsonValue>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QByteArray>
#include <QPair>
#include <QSet>

#include <cmath>
#include <limits>
#include <optional>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcProfile, "phosphoranimation.profile")

// Same rate-limiting shape and rationale as shouldWarnUnknownSequenceMode, for
// the type-rejection path. Keyed on (field, JSON type) so a file carrying two
// differently-malformed fields still reports both, while a reload loop over one
// bad file reports it once.
bool shouldWarnMalformedField(const char* key, QJsonValue::Type type)
{
    static QMutex mutex;
    static QSet<QPair<QByteArray, int>> seen;
    QMutexLocker lock(&mutex);
    const QPair<QByteArray, int> entry{QByteArray(key), static_cast<int>(type)};
    if (seen.contains(entry)) {
        return false;
    }
    seen.insert(entry);
    return true;
}

// Rate limiter for the unresolvable-curve diagnostic. Keyed on the spec string
// rather than a field/type pair, because the interesting axis here is WHICH
// curve name failed. Bounded by the number of distinct bad specs a corpus
// contains, which in practice is one or two typos.
bool shouldWarnMalformedCurveSpec(const QString& spec)
{
    static QMutex mutex;
    static QSet<QString> seen;
    QMutexLocker lock(&mutex);
    if (seen.contains(spec)) {
        return false;
    }
    seen.insert(spec);
    return true;
}

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

    // Reads a numeric field, or returns nullopt when the key holds something
    // that is not a JSON number. Type-checked because `QJsonValue::toDouble(d)`
    // hands back the DEFAULT for any non-number, and every library default
    // passes the range checks below — so `{"duration": "fast"}` would land as
    // an ENGAGED library default, which blocks inheritance instead of falling
    // through to the parent. A malformed value must not quietly pin a node.
    // `presetName` has always type-checked for the same reason; this makes the
    // four numeric fields consistent with it.
    const auto numericField = [&obj](const char* key) -> std::optional<qreal> {
        const QJsonValue v = obj.value(QLatin1String(key));
        if (!v.isDouble()) {
            // Rate-limited for the same reason the unknown-sequenceMode warning
            // is, and against a worse trigger rate: `Settings::animationProfile()`
            // routes through here with no cache and the daemon republishes on the
            // settings slider's drag path, so one hand-edited value would
            // otherwise emit this tens of times a second for the length of a drag.
            if (shouldWarnMalformedField(key, v.type())) {
                qCWarning(lcProfile).nospace() << "Profile::fromJson: ignoring non-numeric " << key
                                               << " (type=" << v.type() << ") — the field will be inherited";
            }
            return std::nullopt;
        }
        return v.toDouble();
    };

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
            if (!p.curve && shouldWarnMalformedCurveSpec(spec)) {
                // Rate-limited like the numeric-field diagnostics, and for a
                // sharper reason: a Global profile naming a user curve that is
                // not registered YET is a documented normal state during
                // startup (the curve loader runs before the profile loader),
                // and a permanently typo'd name would otherwise warn once per
                // parse — i.e. tens of times a second across a settings drag.
                qCWarning(lcProfile).nospace() << "Profile::fromJson: curve spec '" << spec
                                               << "' did not resolve — keeping profile without a curve "
                                                  "(library default will apply at animation time)";
            }
        }
    }

    if (obj.contains(QLatin1String(JsonFieldDuration))) {
        // A non-numeric value returns nullopt and has ALREADY been reported by
        // numericField, so the range check below runs only for real numbers. A
        // second "rejecting duration nan" line would name a value the file
        // never contained.
        if (const std::optional<qreal> value = numericField(JsonFieldDuration)) {
            const qreal raw = *value;
            // Reject NaN / infinity / non-positive / absurdly-large values.
            // The downstream qRound() into int and QQuickPropertyAnimation::
            // setDuration(int) have no tolerance for these — NaN rounds to
            // UB, negatives are silently treated as 0, and large doubles
            // overflow the int conversion. Leaving the field unset makes
            // `effectiveDuration()` substitute the library default, which
            // is the correct fallback for garbage input.
            if (!std::isfinite(raw) || raw <= 0.0 || raw > Profile::MaxDurationMs) {
                qCWarning(lcProfile).nospace()
                    << "Profile::fromJson: rejecting duration " << raw
                    << " (expected 0 < duration <= " << Profile::MaxDurationMs << " ms) — library default will apply";
            } else {
                p.duration = raw;
            }
        }
    }
    if (obj.contains(QLatin1String(JsonFieldMinDistance))) {
        // `QJsonValue::toInt(default)` returns the default verbatim when
        // the underlying value is a JSON double (even for whole-number-
        // valued doubles like `5.0`), so a file written by a non-C++
        // serializer that emitted the integer as a JSON Number would
        // silently fall back to the library default. Route through
        // toDouble() and round to int so `5.0` and `5` both produce 5.
        const std::optional<qreal> minDistanceValue = numericField(JsonFieldMinDistance);
        const qreal rawDouble = minDistanceValue.value_or(std::numeric_limits<qreal>::quiet_NaN());
        // Negative minDistance would make the distance-skip check
        // trivially true for every animation (no real distance is
        // less than a negative threshold), effectively disabling the
        // skip everywhere. Zero is the documented "no skip" value
        // and is accepted.
        //
        // Range-checked BEFORE the round, like the duration and
        // staggerInterval branches: `qRound` on a non-finite or
        // out-of-int-range double is undefined behaviour, and this
        // value comes from a file a user can hand-edit. Checking the
        // rounded result instead only worked by accident, on the
        // platform where the UB conversion happens to pin to INT_MIN.
        if (!minDistanceValue) {
            // Already reported by numericField as non-numeric. Falling into the
            // range branch would emit a SECOND, unrate-limited line naming
            // `nan` — a value the file never contained — on the same ~30 Hz
            // republish path the rate limiter exists to protect.
        } else if (!std::isfinite(rawDouble) || rawDouble < 0.0 || rawDouble > qreal(MaxMinDistancePx)) {
            qCWarning(lcProfile).nospace()
                << "Profile::fromJson: rejecting minDistance " << rawDouble
                << " (expected 0 <= minDistance <= " << MaxMinDistancePx << " px) — library default will apply";
        } else {
            p.minDistance = qRound(rawDouble);
        }
    }
    if (obj.contains(QLatin1String(JsonFieldSequenceMode))) {
        // toDouble + qRound (not toInt): handles `0.0` from non-C++
        // serializers without falling back to the toInt(default) path.
        // Same rationale as minDistance / staggerInterval.
        // A non-number lands on NaN, which fails the roundable test below and
        // takes the substituting branch — sequenceMode keeps its documented
        // engaged-default behaviour rather than being left unset. `wasNumeric`
        // only suppresses the second diagnostic: numericField has already named
        // the field, and "rejecting sequenceMode nan" would report a value the
        // file never contained.
        const std::optional<qreal> sequenceValue = numericField(JsonFieldSequenceMode);
        const bool wasNumeric = sequenceValue.has_value();
        const qreal rawDouble = sequenceValue.value_or(std::numeric_limits<qreal>::quiet_NaN());
        // Range-checked as well as finiteness-checked, like the minDistance and
        // staggerInterval branches: `std::isfinite` alone still admits 1e300,
        // and `qRound` on a value outside the int range is undefined behaviour.
        //
        // The band is half a unit inside the int range on both ends, because
        // Qt's `qRound(double)` is `int(d + 0.5)` for non-negative d and
        // `int(d - 0.5)` otherwise. Testing against INT_MAX itself would admit
        // 2147483647.0, whose `d + 0.5` is out of int range and so is UB in the
        // conversion — the same off-by-a-rounding-step the minDistance branch
        // was already fixed for.
        const bool roundable = std::isfinite(rawDouble) && rawDouble > qreal(std::numeric_limits<int>::min()) + 0.5
            && rawDouble < qreal(std::numeric_limits<int>::max()) - 0.5;
        if (!roundable && !wasNumeric) {
            // Non-numeric: numericField already reported it. Substitute the
            // engaged default and move on without a second diagnostic.
            p.sequenceMode = DefaultSequenceMode;
        } else if (!roundable) {
            // Its own diagnostic, distinct from the unknown-enumerator one
            // below, and shaped like the duration / minDistance / stagger
            // rejections. Routing this case through the unknown-enumerator
            // warning instead would report a rounded value that was never
            // written, and would collapse every out-of-range input onto one
            // rate-limiter key so only the first ever warned.
            qCWarning(lcProfile).nospace() << "Profile::fromJson: rejecting sequenceMode " << rawDouble
                                           << " (out of int range) — substituting DefaultSequenceMode";
            // Substituted ENGAGED rather than left unset, unlike the three
            // scalar validators: see the note on this field in the header.
            p.sequenceMode = DefaultSequenceMode;
        } else {
            const int raw = qRound(rawDouble);
            // Map valid enumerators; anything else falls back to the library
            // default. This is NOT forward-compat with future enumerators
            // written by a newer client — those land on AllAtOnce rather than
            // on a behaviorally-similar mode, which is why the warning below
            // is not optional. If new modes are added, bump the schema and
            // route through migration code.
            if (raw == static_cast<int>(SequenceMode::AllAtOnce) || raw == static_cast<int>(SequenceMode::Cascade)) {
                p.sequenceMode = static_cast<SequenceMode>(raw);
            } else {
                // Log so schema drift doesn't paper over as "AllAtOnce" — a
                // newer client writing an unknown enumerator is something a
                // future-maintainer wants to see in logs, not discover via a
                // mysterious animation-behaviour regression. Rate-limited to
                // one message per distinct value per process (see
                // shouldWarnUnknownSequenceMode). The set is bounded by the
                // number of DISTINCT in-range unknown enumerators a corpus
                // contains, which in practice is a handful.
                if (shouldWarnUnknownSequenceMode(raw)) {
                    qCWarning(lcProfile) << "Profile::fromJson: unknown sequenceMode" << raw
                                         << "— substituting DefaultSequenceMode (schema drift?)";
                }
                p.sequenceMode = DefaultSequenceMode;
            }
        }
    }
    if (obj.contains(QLatin1String(JsonFieldStaggerInterval))) {
        // toDouble + qRound (not toInt): a JSON Number written as `30.0`
        // by a non-C++ serializer round-trips as 30 instead of falling
        // back to the toInt(default) path. Same shape as minDistance.
        const std::optional<qreal> staggerValue = numericField(JsonFieldStaggerInterval);
        const qreal rawDouble = staggerValue.value_or(std::numeric_limits<qreal>::quiet_NaN());
        if (!staggerValue) {
            // Same as minDistance above: already reported, and the range branch
            // would name a `nan` the file never contained.
        } else if (!std::isfinite(rawDouble) || rawDouble < 0.0
                   || rawDouble > static_cast<qreal>(MaxStaggerIntervalMs)) {
            // Negative stagger would make every cascade element fire
            // immediately (its scheduled-start would already be in the
            // past), erasing the cascade visual. An hour-plus stagger
            // would freeze the cascade for any practical animation.
            // Same shape and rationale as the duration / minDistance /
            // sequenceMode validators above — leaving the field unset
            // routes `effectiveStaggerInterval()` to the library default.
            qCWarning(lcProfile).nospace()
                << "Profile::fromJson: rejecting staggerInterval " << rawDouble
                << " (expected 0 <= staggerInterval <= " << MaxStaggerIntervalMs << " ms) — library default will apply";
        } else {
            p.staggerInterval = qRound(rawDouble);
        }
    }
    if (obj.contains(QLatin1String(JsonFieldPresetName))) {
        // `toString()` on a non-string QJsonValue returns an empty
        // QString, which — when assigned to `p.presetName` — engages
        // the optional with an empty value. That's semantically
        // distinct from "unset" (engaged-empty means "explicit empty
        // override"; unset means "inherit from parent"), so a JSON
        // file with `"presetName": 42` would silently become
        // `presetName = ""` instead of leaving the optional
        // disengaged. Guard on `isString()` so only genuine strings
        // engage the optional.
        const QJsonValue v = obj.value(QLatin1String(JsonFieldPresetName));
        if (v.isString()) {
            p.presetName = v.toString();
        } else {
            if (shouldWarnMalformedField(JsonFieldPresetName, v.type())) {
                qCWarning(lcProfile).nospace()
                    << "Profile::fromJson: ignoring non-string presetName (type=" << v.type() << ")";
            }
        }
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
