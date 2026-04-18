// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>

#include <QHash>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>

#include <vector>

namespace PhosphorAnimation {

Q_LOGGING_CATEGORY(lcCurveRegistry, "phosphoranimation.curveregistry")

// ═══════════════════════════════════════════════════════════════════════════════
// Impl — private state
// ═══════════════════════════════════════════════════════════════════════════════

class CurveRegistry::Impl
{
public:
    mutable QMutex mutex;
    // insertionOrder keeps knownTypes() stable across platforms; factories
    // is the actual lookup. A QHash alone would be unordered and make
    // snapshot tests brittle.
    std::vector<QString> insertionOrder;
    QHash<QString, Factory> factories;

    void registerBuiltins();
};

void CurveRegistry::Impl::registerBuiltins()
{
    // Cubic-bezier wire format is bare 4-comma: pass params directly to
    // Easing::fromString (which dispatches on "no letters" → bezier).
    Factory bezierFactory = [](const QString&, const QString& params) -> std::shared_ptr<const Curve> {
        return std::make_shared<Easing>(Easing::fromString(params));
    };
    insertionOrder.push_back(QStringLiteral("bezier"));
    factories.insert(QStringLiteral("bezier"), bezierFactory);

    // Elastic + bounce variants use "name:params" — Easing::fromString
    // needs the typeId in the spec to choose the right Type enumerator.
    Factory namedEasingFactory = [](const QString& typeId, const QString& params) -> std::shared_ptr<const Curve> {
        const QString spec = params.isEmpty() ? typeId : (typeId + QLatin1Char(':') + params);
        return std::make_shared<Easing>(Easing::fromString(spec));
    };
    const QStringList namedIds{
        QStringLiteral("elastic-in"), QStringLiteral("elastic-out"), QStringLiteral("elastic-in-out"),
        QStringLiteral("bounce-in"),  QStringLiteral("bounce-out"),  QStringLiteral("bounce-in-out"),
    };
    for (const QString& id : namedIds) {
        insertionOrder.push_back(id);
        factories.insert(id, namedEasingFactory);
    }

    // Spring: single typeId, dedicated factory.
    insertionOrder.push_back(QStringLiteral("spring"));
    factories.insert(QStringLiteral("spring"),
                     [](const QString& typeId, const QString& params) -> std::shared_ptr<const Curve> {
                         const QString spec = params.isEmpty() ? typeId : (typeId + QLatin1Char(':') + params);
                         return std::make_shared<Spring>(Spring::fromString(spec));
                     });
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

CurveRegistry& CurveRegistry::instance()
{
    // Meyers singleton — C++11 guarantees thread-safe initialization.
    // Avoids Q_GLOBAL_STATIC's static-destruction ordering pitfalls for
    // a library that may be unloaded before Qt's plugin-teardown.
    static CurveRegistry sInstance;
    return sInstance;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Registration
// ═══════════════════════════════════════════════════════════════════════════════

bool CurveRegistry::registerFactory(const QString& typeId, Factory factory)
{
    if (typeId.isEmpty() || !factory) {
        qCWarning(lcCurveRegistry) << "registerFactory: empty typeId or null factory rejected";
        return false;
    }

    QMutexLocker locker(&m_impl->mutex);
    const bool replaced = m_impl->factories.contains(typeId);
    if (!replaced) {
        m_impl->insertionOrder.push_back(typeId);
    }
    m_impl->factories.insert(typeId, std::move(factory));
    return replaced;
}

bool CurveRegistry::unregisterFactory(const QString& typeId)
{
    QMutexLocker locker(&m_impl->mutex);
    if (!m_impl->factories.remove(typeId)) {
        return false;
    }
    auto it = std::find(m_impl->insertionOrder.begin(), m_impl->insertionOrder.end(), typeId);
    if (it != m_impl->insertionOrder.end()) {
        m_impl->insertionOrder.erase(it);
    }
    return true;
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

ParsedSpec parseSpec(const QString& spec)
{
    ParsedSpec out;
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
        const QString prefix = spec.left(colonIdx).trimmed();
        // Cubic-bezier has exactly one wire format — the bare 4-comma
        // form. The "bezier:..." prefixed form is intentionally rejected
        // here so the registry and Easing::fromString agree.
        if (prefix == QLatin1String("bezier")) {
            return out; // empty typeId → unknown
        }
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

    Factory factory;
    {
        QMutexLocker locker(&m_impl->mutex);
        factory = m_impl->factories.value(parsed.typeId);
    }

    if (!factory) {
        return nullptr;
    }

    return factory(parsed.typeId, parsed.params);
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

    Factory factory;
    {
        QMutexLocker locker(&m_impl->mutex);
        factory = m_impl->factories.value(parsed.typeId);
    }

    if (!factory) {
        qCWarning(lcCurveRegistry) << "unknown curve typeId" << parsed.typeId << "in spec" << spec
                                   << "- returning default";
        return std::make_shared<Easing>();
    }

    auto curve = factory(parsed.typeId, parsed.params);
    if (!curve) {
        qCWarning(lcCurveRegistry) << "factory for" << parsed.typeId << "returned null for params" << parsed.params
                                   << "- returning default";
        return std::make_shared<Easing>();
    }
    return curve;
}

QStringList CurveRegistry::knownTypes() const
{
    QMutexLocker locker(&m_impl->mutex);
    QStringList result;
    result.reserve(static_cast<int>(m_impl->insertionOrder.size()));
    for (const QString& id : m_impl->insertionOrder) {
        result.append(id);
    }
    return result;
}

bool CurveRegistry::has(const QString& typeId) const
{
    QMutexLocker locker(&m_impl->mutex);
    return m_impl->factories.contains(typeId);
}

} // namespace PhosphorAnimation
