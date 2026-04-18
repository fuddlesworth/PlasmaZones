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
    // Easing: all 7 variants share one Easing::fromString helper — it
    // already handles the full "name:params" or bare numeric dispatch.
    Factory easingFactory = [](const QString& typeId, const QString& params) -> std::shared_ptr<const Curve> {
        const QString spec = params.isEmpty() ? typeId : (typeId + QLatin1Char(':') + params);
        return std::make_shared<Easing>(Easing::fromString(spec));
    };

    const QStringList easingIds{
        QStringLiteral("bezier"),         QStringLiteral("elastic-in"), QStringLiteral("elastic-out"),
        QStringLiteral("elastic-in-out"), QStringLiteral("bounce-in"),  QStringLiteral("bounce-out"),
        QStringLiteral("bounce-in-out"),
    };
    for (const QString& id : easingIds) {
        insertionOrder.push_back(id);
        factories.insert(id, easingFactory);
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

std::shared_ptr<const Curve> CurveRegistry::create(const QString& spec) const
{
    if (spec.isEmpty()) {
        // No default configured — caller handles fallback.
        return nullptr;
    }

    // Legacy bare-bezier form: four comma-separated numbers, no colon.
    // Dispatch to the "bezier" factory with the spec as-is.
    const int colonIdx = spec.indexOf(QLatin1Char(':'));
    QString typeId;
    QString params;
    if (colonIdx < 0) {
        // Might be legacy bezier, or a bare typeId with no params.
        if (spec.count(QLatin1Char(',')) == 3) {
            typeId = QStringLiteral("bezier");
            params = spec;
        } else {
            typeId = spec.trimmed();
            params = QString();
        }
    } else {
        typeId = spec.left(colonIdx).trimmed();
        params = spec.mid(colonIdx + 1).trimmed();
    }

    Factory factory;
    {
        QMutexLocker locker(&m_impl->mutex);
        factory = m_impl->factories.value(typeId);
    }

    if (!factory) {
        qCWarning(lcCurveRegistry) << "unknown curve typeId" << typeId << "in spec" << spec << "- returning default";
        return std::make_shared<Easing>();
    }

    auto curve = factory(typeId, params);
    if (!curve) {
        qCWarning(lcCurveRegistry) << "factory for" << typeId << "returned null for params" << params
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
