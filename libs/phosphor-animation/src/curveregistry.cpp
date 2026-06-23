// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/Registry.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QSet>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <memory>

namespace PhosphorAnimation {

Q_LOGGING_CATEGORY(lcCurveRegistry, "phosphoranimation.curveregistry")

// ═══════════════════════════════════════════════════════════════════════════════
// Impl — private state
// ═══════════════════════════════════════════════════════════════════════════════

/// Registry entry: one typeId's string-form factory (primary dispatch used by
/// create/tryCreate), an optional JSON-parameter factory (the loader's
/// parseFile path), wrapped as a PhosphorRegistry factory so the registry's
/// generic storage + thread-safety + owner-tagged unregister + replace-on-
/// reregister back the curve catalogue instead of a hand-rolled mutex+QHash.
/// displayName() is the typeId (curves carry no separate human label);
/// capabilities() defaults to {}.
class CurveFactoryEntry final : public PhosphorRegistry::IFactoryBase
{
public:
    CurveFactoryEntry(QString typeId, CurveRegistry::Factory stringFactory, CurveRegistry::JsonFactory jsonFactory)
        : stringFactory(std::move(stringFactory))
        , jsonFactory(std::move(jsonFactory))
        , m_typeId(std::move(typeId))
    {
    }
    [[nodiscard]] QString id() const override
    {
        return m_typeId;
    }
    [[nodiscard]] QString displayName() const override
    {
        return m_typeId;
    }

    CurveRegistry::Factory stringFactory;
    CurveRegistry::JsonFactory jsonFactory; ///< may be null for string-only registrations

private:
    QString m_typeId;
};

class CurveRegistry::Impl
{
public:
    // Generic, thread-safe id-keyed store. Replaces the former
    // mutex + insertionOrder + QHash<typeId, Entry> hand-roll.
    PhosphorRegistry::Registry<CurveFactoryEntry> registry;

    void registerBuiltins();
};

namespace {

// Shared validation + wire-format builders for the built-in JSON
// factories. The loader's former per-typeId parameter parsing in
// CurveLoader::Sink::parseFile duplicated this schema; hoisting it
// here keeps the registry as the sole schema authority — third-party
// curves can register a `JsonFactory` and become JSON-loadable through
// the loader automatically.
//
// Sane upper bounds for built-in curve parameters. The downstream
// C++ ctors (Spring::fromString, Easing::fromString) already qBound
// to a tighter sub-range; the validator's job here is to reject
// pathological JSON (NaN/inf, multi-million amplitude typos) before
// the parser silently substitutes a library default, so the user gets
// an actionable warning. Bounds are deliberately ~5× past the ctor
// clamp so well-meaning but out-of-range values still flow through to
// the ctor's silent clamp instead of being rejected here.
constexpr qreal kMaxOmega = 1000.0; // Spring ctor clamps to [0.1, 200]
constexpr qreal kMaxZeta = 100.0; // Spring ctor clamps to [0, 10]
constexpr qreal kMaxAmplitude = 1000.0; // Easing ctor clamps elastic to [0.5, 3], bounce > 0
constexpr qreal kMaxPeriod = 1000.0; // Easing ctor clamps elastic to [0.1, 1]
constexpr int kMaxBounces = 1000; // Easing ctor clamps to [1, 8]

bool validateSpringParams(const QJsonObject& params, qreal& omega, qreal& zeta)
{
    omega = params.value(QLatin1String("omega")).toDouble(12.0);
    zeta = params.value(QLatin1String("zeta")).toDouble(0.8);
    if (!std::isfinite(omega) || omega <= 0.0 || omega > kMaxOmega) {
        qCWarning(lcCurveRegistry) << "spring omega must be finite and in (0," << kMaxOmega << "], got" << omega;
        return false;
    }
    if (!std::isfinite(zeta) || zeta < 0.0 || zeta > kMaxZeta) {
        qCWarning(lcCurveRegistry) << "spring zeta must be finite and in [0," << kMaxZeta << "], got" << zeta;
        return false;
    }
    return true;
}

bool validateCubicBezierParams(const QJsonObject& params, qreal& x1, qreal& y1, qreal& x2, qreal& y2)
{
    x1 = params.value(QLatin1String("x1")).toDouble(0.33);
    y1 = params.value(QLatin1String("y1")).toDouble(1.0);
    x2 = params.value(QLatin1String("x2")).toDouble(0.68);
    y2 = params.value(QLatin1String("y2")).toDouble(1.0);
    // Cubic Bezier control points: x must stay in [0,1] (CSS spec +
    // QEasingCurve constraint). y can legitimately exceed the unit
    // range — that's how overshoot curves work — so y1/y2 are bounded
    // to the same [-1, 2] range as the parse-path clamp in
    // Easing::fromString. NaN/inf are rejected for both axes — the
    // downstream evaluator has no defined behaviour on non-finite
    // control points and would either UB or render visible glitches.
    if (!std::isfinite(x1) || x1 < 0.0 || x1 > 1.0) {
        qCWarning(lcCurveRegistry) << "cubic-bezier x1 must be finite and in [0, 1], got" << x1;
        return false;
    }
    if (!std::isfinite(x2) || x2 < 0.0 || x2 > 1.0) {
        qCWarning(lcCurveRegistry) << "cubic-bezier x2 must be finite and in [0, 1], got" << x2;
        return false;
    }
    if (!std::isfinite(y1) || y1 < -1.0 || y1 > 2.0) {
        qCWarning(lcCurveRegistry) << "cubic-bezier y1 must be finite and in [-1, 2], got" << y1;
        return false;
    }
    if (!std::isfinite(y2) || y2 < -1.0 || y2 > 2.0) {
        qCWarning(lcCurveRegistry) << "cubic-bezier y2 must be finite and in [-1, 2], got" << y2;
        return false;
    }
    return true;
}

bool validateElasticParams(const QJsonObject& params, qreal& amplitude, qreal& period)
{
    amplitude = params.value(QLatin1String("amplitude")).toDouble(1.0);
    period = params.value(QLatin1String("period")).toDouble(0.3);
    if (!std::isfinite(amplitude) || amplitude <= 0.0 || amplitude > kMaxAmplitude) {
        qCWarning(lcCurveRegistry) << "elastic amplitude must be finite and in (0," << kMaxAmplitude << "], got"
                                   << amplitude;
        return false;
    }
    if (!std::isfinite(period) || period <= 0.0 || period > kMaxPeriod) {
        qCWarning(lcCurveRegistry) << "elastic period must be finite and in (0," << kMaxPeriod << "], got" << period;
        return false;
    }
    return true;
}

bool validateBounceParams(const QJsonObject& params, qreal& amplitude, int& bounces)
{
    amplitude = params.value(QLatin1String("amplitude")).toDouble(1.0);
    // toDouble + qRound (not toInt) so a JSON Number written as `3.0`
    // by a non-C++ serializer round-trips as 3 instead of falling back
    // to the toInt(default) = 3 path. Matches the qRound-on-toDouble
    // pattern used for minDistance / staggerInterval in profile.cpp.
    const qreal bouncesRaw = params.value(QLatin1String("bounces")).toDouble(3.0);
    if (!std::isfinite(amplitude) || amplitude <= 0.0 || amplitude > kMaxAmplitude) {
        qCWarning(lcCurveRegistry) << "bounce amplitude must be finite and in (0," << kMaxAmplitude << "], got"
                                   << amplitude;
        return false;
    }
    if (!std::isfinite(bouncesRaw) || bouncesRaw < 1.0 || bouncesRaw > static_cast<qreal>(kMaxBounces)) {
        qCWarning(lcCurveRegistry) << "bounce bounces must be finite and in [1," << kMaxBounces << "], got"
                                   << bouncesRaw;
        return false;
    }
    bounces = qRound(bouncesRaw);
    return true;
}

} // namespace

void CurveRegistry::Impl::registerBuiltins()
{
    // Cubic-bezier wire format is bare 4-comma: pass params directly to
    // Easing::fromString (which dispatches on "no letters" → bezier).
    //
    // Register under BOTH "bezier" and "cubic-bezier" — the loader and the
    // build-time check-animation-profiles.py validator both treat
    // "cubic-bezier" as a known builtin (CurveLoader::Sink::parseFile
    // accepts `"typeId": "cubic-bezier"`, isBuiltinTypeId() lists it,
    // BUILTIN_CURVE_TYPEIDS contains it). Without the alias, a profile
    // JSON with `"curve": "cubic-bezier:x1,y1,x2,y2"` passes the build
    // check but resolves to nullptr at runtime (parseSpec splits on the
    // colon → typeId="cubic-bezier" → factory miss), and the animation
    // silently falls back to OutCubic with only a debug log.
    //
    // Each built-in registers BOTH a string-form Factory and a
    // JSON-parameter JsonFactory. The JSON side converts the parameters
    // object into the canonical wire spec and delegates to
    // `Easing::fromString` / `Spring::fromString` — the parameter
    // schema lives in the registry, so `CurveLoader::Sink::parseFile`
    // just forwards to `tryCreateFromJson` without duplicating the
    // cases.
    Factory bezierFactory = [](const QString&, const QString& params) -> std::shared_ptr<const Curve> {
        return std::make_shared<Easing>(Easing::fromString(params));
    };
    JsonFactory bezierJson = [](const QJsonObject& params) -> std::shared_ptr<const Curve> {
        qreal x1, y1, x2, y2;
        if (!validateCubicBezierParams(params, x1, y1, x2, y2)) {
            return nullptr;
        }
        const QString spec = QStringLiteral("%1,%2,%3,%4").arg(x1).arg(y1).arg(x2).arg(y2);
        return std::make_shared<Easing>(Easing::fromString(spec));
    };
    registry.registerFactory(std::make_shared<CurveFactoryEntry>(QStringLiteral("bezier"), bezierFactory, bezierJson));
    registry.registerFactory(
        std::make_shared<CurveFactoryEntry>(QStringLiteral("cubic-bezier"), bezierFactory, bezierJson));

    // Elastic variants use "name:params" — Easing::fromString needs the
    // typeId in the spec to choose the right Type enumerator.
    Factory namedEasingFactory = [](const QString& typeId, const QString& params) -> std::shared_ptr<const Curve> {
        const QString spec = params.isEmpty() ? typeId : (typeId + QLatin1Char(':') + params);
        return std::make_shared<Easing>(Easing::fromString(spec));
    };

    const QStringList elasticIds{
        QStringLiteral("elastic-in"),
        QStringLiteral("elastic-out"),
        QStringLiteral("elastic-in-out"),
    };
    for (const QString& id : elasticIds) {
        JsonFactory elasticJson = [id](const QJsonObject& params) -> std::shared_ptr<const Curve> {
            qreal amplitude, period;
            if (!validateElasticParams(params, amplitude, period)) {
                return nullptr;
            }
            const QString spec = QStringLiteral("%1:%2,%3").arg(id).arg(amplitude).arg(period);
            return std::make_shared<Easing>(Easing::fromString(spec));
        };
        registry.registerFactory(std::make_shared<CurveFactoryEntry>(id, namedEasingFactory, elasticJson));
    }

    const QStringList bounceIds{
        QStringLiteral("bounce-in"),
        QStringLiteral("bounce-out"),
        QStringLiteral("bounce-in-out"),
    };
    for (const QString& id : bounceIds) {
        JsonFactory bounceJson = [id](const QJsonObject& params) -> std::shared_ptr<const Curve> {
            qreal amplitude;
            int bounces;
            if (!validateBounceParams(params, amplitude, bounces)) {
                return nullptr;
            }
            const QString spec = QStringLiteral("%1:%2,%3").arg(id).arg(amplitude).arg(bounces);
            return std::make_shared<Easing>(Easing::fromString(spec));
        };
        registry.registerFactory(std::make_shared<CurveFactoryEntry>(id, namedEasingFactory, bounceJson));
    }

    // Spring: single typeId, dedicated factory.
    Factory springFactory = [](const QString& typeId, const QString& params) -> std::shared_ptr<const Curve> {
        const QString spec = params.isEmpty() ? typeId : (typeId + QLatin1Char(':') + params);
        return std::make_shared<Spring>(Spring::fromString(spec));
    };
    JsonFactory springJson = [](const QJsonObject& params) -> std::shared_ptr<const Curve> {
        qreal omega, zeta;
        if (!validateSpringParams(params, omega, zeta)) {
            return nullptr;
        }
        const QString spec = QStringLiteral("spring:%1,%2").arg(omega).arg(zeta);
        return std::make_shared<Spring>(Spring::fromString(spec));
    };
    registry.registerFactory(std::make_shared<CurveFactoryEntry>(QStringLiteral("spring"), springFactory, springJson));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

CurveRegistry::CurveRegistry()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->registerBuiltins();
}

CurveRegistry::~CurveRegistry() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Registration
// ═══════════════════════════════════════════════════════════════════════════════

bool CurveRegistry::registerFactory(const QString& typeId, Factory factory)
{
    return registerFactory(typeId, std::move(factory), JsonFactory{}, QString());
}

bool CurveRegistry::registerFactory(const QString& typeId, Factory factory, const QString& ownerTag)
{
    return registerFactory(typeId, std::move(factory), JsonFactory{}, ownerTag);
}

bool CurveRegistry::registerFactory(const QString& typeId, Factory stringFactory, JsonFactory jsonFactory,
                                    const QString& ownerTag)
{
    if (typeId.isEmpty() || !stringFactory) {
        qCWarning(lcCurveRegistry) << "registerFactory: empty typeId or null factory rejected";
        return false;
    }
    // Returns whether a prior registration was overwritten (the documented
    // contract). The registry's Replace policy overwrites in place; check
    // first since registerFactory(...Replace) returns success, not replaced.
    // The check + Replace are two separate registry lock acquisitions, so the
    // returned bool is exact only for single-threaded registration (the
    // composition-root / loader case — see CurveRegistry.h); a concurrent
    // mutation of the same typeId between them could make it stale. The
    // storage itself stays consistent regardless (each call is atomic).
    const bool replaced = m_impl->registry.factory(typeId) != nullptr;
    m_impl->registry.registerFactory(
        std::make_shared<CurveFactoryEntry>(typeId, std::move(stringFactory), std::move(jsonFactory)), ownerTag,
        PhosphorRegistry::DuplicatePolicy::Replace);
    return replaced;
}

bool CurveRegistry::unregisterFactory(const QString& typeId)
{
    return m_impl->registry.unregisterFactory(typeId);
}

int CurveRegistry::unregisterByOwner(const QString& ownerTag)
{
    // The registry rejects an empty tag (it would otherwise wipe every
    // untagged built-in) and emits per-entry factoryUnregistered signals.
    return m_impl->registry.unregisterByOwner(ownerTag);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup + parse
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

struct ParsedSpec
{
    QString typeId;
    QString params;
};

// A canonical built-in spec ("elastic-in-out:1.000,0.300") is ~24
// chars. 512 is generous (covers any plausible third-party typeId
// with several params) while still rejecting hand-crafted multi-MB
// strings whose only purpose is to burn CPU through indexOf/count.
constexpr int kMaxSpecLength = 512;

ParsedSpec parseSpec(const QString& spec)
{
    ParsedSpec out;
    if (spec.size() > kMaxSpecLength) {
        qCWarning(lcCurveRegistry).nospace()
            << "parseSpec: rejecting curve spec of length " << spec.size() << " (max " << kMaxSpecLength << ")";
        return out;
    }
    const int colonIdx = spec.indexOf(QLatin1Char(':'));
    if (colonIdx < 0) {
        // Either the bare cubic-bezier wire format ("x1,y1,x2,y2"), or
        // a bare typeId with no params. Exactly three commas means four
        // numbers → bezier.
        if (spec.count(QLatin1Char(',')) == 3) {
            out.typeId = QStringLiteral("bezier");
            out.params = spec;
        } else {
            out.typeId = spec.trimmed();
        }
    } else {
        // Lower-case the prefix. Factory typeIds are registered
        // lower-case by convention ("bezier", "spring", "elastic-in"…) —
        // matching case-insensitively lets hand-written configs using
        // "Bezier:..." or "SPRING:..." round-trip instead of silently
        // falling through to the OutCubic default.
        const QString prefix = spec.left(colonIdx).trimmed().toLower();
        out.typeId = prefix;
        out.params = spec.mid(colonIdx + 1).trimmed();
    }
    return out;
}

} // namespace

std::shared_ptr<const Curve> CurveRegistry::tryCreate(const QString& spec) const
{
    if (spec.isEmpty()) {
        return nullptr;
    }

    const ParsedSpec parsed = parseSpec(spec);

    const auto entry = m_impl->registry.factory(parsed.typeId);
    if (!entry || !entry->stringFactory) {
        return nullptr;
    }
    return entry->stringFactory(parsed.typeId, parsed.params);
}

std::shared_ptr<const Curve> CurveRegistry::tryCreateFromJson(const QString& typeId,
                                                              const QJsonObject& parameters) const
{
    if (typeId.isEmpty()) {
        return nullptr;
    }

    const auto entry = m_impl->registry.factory(typeId);
    if (!entry || !entry->jsonFactory) {
        return nullptr;
    }
    return entry->jsonFactory(parameters);
}

std::shared_ptr<const Curve> CurveRegistry::create(const QString& spec) const
{
    if (spec.isEmpty()) {
        // Empty spec → same fallback as unknown typeId. Previously this
        // branch returned nullptr and the caller had to guard, but the
        // single `animationEasingCurve` setting round-trips through
        // empty during config reload / migration / settings-UI edits,
        // and a null curve means linear progression in
        // WindowMotion::updateProgress — which is a visible regression
        // from the pre-registry default OutCubic bezier. Match the
        // unknown-typeId branch's behaviour so callers don't need
        // parallel null-guards.
        return std::make_shared<Easing>();
    }

    const ParsedSpec parsed = parseSpec(spec);

    const auto entry = m_impl->registry.factory(parsed.typeId);
    if (!entry || !entry->stringFactory) {
        qCWarning(lcCurveRegistry) << "unknown curve typeId" << parsed.typeId << "in spec" << spec
                                   << "- returning default";
        return std::make_shared<Easing>();
    }

    auto curve = entry->stringFactory(parsed.typeId, parsed.params);
    if (!curve) {
        qCWarning(lcCurveRegistry) << "factory for" << parsed.typeId << "returned null for params" << parsed.params
                                   << "- returning default";
        return std::make_shared<Easing>();
    }
    return curve;
}

QStringList CurveRegistry::knownTypes() const
{
    // The registry returns ids in insertion order; sort for a stable
    // alphabetical order. Consumers — only tests today — check membership,
    // not order.
    QStringList result = m_impl->registry.ids();
    std::sort(result.begin(), result.end());
    return result;
}

bool CurveRegistry::has(const QString& typeId) const
{
    return m_impl->registry.factory(typeId) != nullptr;
}

bool CurveRegistry::isBuiltinTypeId(const QString& typeId)
{
    // Mirrors Impl::registerBuiltins. Kept in a file-scope static so
    // it is constructed once per process and its contains() cost is
    // O(log N) instead of O(N) per check. Adding a builtin requires
    // updating both this set and registerBuiltins — a deliberate
    // symmetry so the list can't rot unnoticed (a new builtin that is
    // NOT in this set would silently be overridable by a user JSON).
    static const QSet<QString> kBuiltins = {
        QStringLiteral("bezier"),     QStringLiteral("cubic-bezier"), QStringLiteral("spring"),
        QStringLiteral("elastic-in"), QStringLiteral("elastic-out"),  QStringLiteral("elastic-in-out"),
        QStringLiteral("bounce-in"),  QStringLiteral("bounce-out"),   QStringLiteral("bounce-in-out"),
    };
    return kBuiltins.contains(typeId);
}

} // namespace PhosphorAnimation
