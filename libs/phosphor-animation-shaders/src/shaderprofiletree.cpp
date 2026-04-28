// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/ShaderProfileTree.h>

#include <PhosphorAnimation/ProfilePaths.h>

#include <QJsonArray>
#include <QJsonValue>

namespace PhosphorAnimationShaders {

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

ShaderProfile ShaderProfileTree::resolve(const QString& path) const
{
    QStringList chain;
    QString cursor = path;
    while (!cursor.isEmpty()) {
        chain.prepend(cursor);
        cursor = PhosphorAnimation::ProfilePaths::parentPath(cursor);
    }

    ShaderProfile effective = m_baseline;

    for (const QString& step : chain) {
        auto it = m_overrides.constFind(step);
        if (it == m_overrides.constEnd())
            continue;
        ShaderProfile::overlay(effective, it.value());
    }

    return effective.withDefaults();
}

ShaderProfile ShaderProfileTree::directOverride(const QString& path) const
{
    return m_overrides.value(path);
}

bool ShaderProfileTree::hasOverride(const QString& path) const
{
    return m_overrides.contains(path);
}

QStringList ShaderProfileTree::overriddenPaths() const
{
    return m_insertionOrder;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mutation
// ═══════════════════════════════════════════════════════════════════════════════

void ShaderProfileTree::setOverride(const QString& path, const ShaderProfile& profile)
{
    if (path.isEmpty())
        return;
    if (!m_overrides.contains(path))
        m_insertionOrder.append(path);
    m_overrides.insert(path, profile);
}

bool ShaderProfileTree::clearOverride(const QString& path)
{
    if (!m_overrides.remove(path))
        return false;
    m_insertionOrder.removeAll(path);
    return true;
}

void ShaderProfileTree::clearAllOverrides()
{
    m_overrides.clear();
    m_insertionOrder.clear();
}

void ShaderProfileTree::setBaseline(const ShaderProfile& profile)
{
    m_baseline = profile;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Serialization
// ═══════════════════════════════════════════════════════════════════════════════

QJsonObject ShaderProfileTree::toJson() const
{
    QJsonObject root;
    root.insert(QLatin1String("baseline"), m_baseline.toJson());

    QJsonArray overrides;
    for (const QString& path : m_insertionOrder) {
        auto it = m_overrides.constFind(path);
        if (it == m_overrides.constEnd())
            continue;
        QJsonObject entry;
        entry.insert(QLatin1String("path"), path);
        entry.insert(QLatin1String("profile"), it.value().toJson());
        overrides.append(entry);
    }
    root.insert(QLatin1String("overrides"), overrides);

    return root;
}

ShaderProfileTree ShaderProfileTree::fromJson(const QJsonObject& obj)
{
    ShaderProfileTree tree;

    if (obj.contains(QLatin1String("baseline")))
        tree.m_baseline = ShaderProfile::fromJson(obj.value(QLatin1String("baseline")).toObject());

    const QJsonArray arr = obj.value(QLatin1String("overrides")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(QLatin1String("path")).toString();
        if (path.isEmpty())
            continue;
        tree.setOverride(path, ShaderProfile::fromJson(entry.value(QLatin1String("profile")).toObject()));
    }

    return tree;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Equality
// ═══════════════════════════════════════════════════════════════════════════════

bool ShaderProfileTree::operator==(const ShaderProfileTree& other) const
{
    if (m_baseline != other.m_baseline)
        return false;
    if (m_insertionOrder != other.m_insertionOrder)
        return false;
    if (m_overrides.size() != other.m_overrides.size())
        return false;
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it) {
        auto otherIt = other.m_overrides.constFind(it.key());
        if (otherIt == other.m_overrides.constEnd())
            return false;
        if (it.value() != otherIt.value())
            return false;
    }
    return true;
}

} // namespace PhosphorAnimationShaders
