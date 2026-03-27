// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SplitTree.h"
#include "core/constants.h"
#include "core/logging.h"

#include <QJsonObject>
#include <QLatin1String>
#include <QSet>

#include <algorithm>

namespace PlasmaZones {

using namespace AutotileDefaults;

// =============================================================================
// Serialization (implementations of SplitTree member functions)
// =============================================================================

QJsonObject SplitTree::toJson() const
{
    QJsonObject json;
    json[QLatin1String("version")] = 1;
    if (m_root) {
        json[QLatin1String("root")] = nodeToJson(m_root.get());
    }
    return json;
}

std::unique_ptr<SplitTree> SplitTree::fromJson(const QJsonObject& json)
{
    if (!json.contains(QLatin1String("root"))) {
        return nullptr;
    }

    auto tree = std::make_unique<SplitTree>();
    const QJsonObject rootObj = json[QLatin1String("root")].toObject();
    int nodeCount = 0;
    QSet<QString> seenIds;
    tree->m_root = nodeFromJson(rootObj, nullptr, 0, nodeCount, seenIds);
    if (!tree->m_root) {
        return nullptr;
    }
    return tree;
}

QJsonObject SplitTree::nodeToJson(const SplitNode* node, int depth)
{
    QJsonObject json;
    if (!node) {
        return json;
    }

    if (depth >= MaxDeserializationDepth) {
        qCWarning(lcAutotile) << "SplitTree::nodeToJson: max depth exceeded, truncating";
        return json;
    }

    if (node->isLeaf()) {
        json[QLatin1String("windowId")] = node->windowId;
    } else {
        json[QLatin1String("ratio")] = node->splitRatio;
        json[QLatin1String("horizontal")] = node->splitHorizontal;
        json[QLatin1String("first")] = nodeToJson(node->first.get(), depth + 1);
        json[QLatin1String("second")] = nodeToJson(node->second.get(), depth + 1);
    }

    return json;
}

std::unique_ptr<SplitNode> SplitTree::nodeFromJson(const QJsonObject& json, SplitNode* parent, int depth,
                                                   int& nodeCount, QSet<QString>& seenIds)
{
    if (json.isEmpty()) {
        return nullptr;
    }

    if (depth >= MaxDeserializationDepth) {
        qCWarning(lcAutotile) << "SplitTree::fromJson: max depth exceeded, truncating";
        return nullptr;
    }

    if (++nodeCount > MaxDeserializationNodes) {
        qCWarning(lcAutotile) << "SplitTree::fromJson: max node count exceeded, truncating";
        return nullptr;
    }

    auto node = std::make_unique<SplitNode>();
    node->parent = parent;

    if (json.contains(QLatin1String("first")) && json.contains(QLatin1String("second"))) {
        // Only set split properties on internal nodes (meaningless on leaves)
        node->splitRatio = std::clamp(json[QLatin1String("ratio")].toDouble(0.5), MinSplitRatio, MaxSplitRatio);
        node->splitHorizontal = json[QLatin1String("horizontal")].toBool(false);
        // Internal node
        node->first = nodeFromJson(json[QLatin1String("first")].toObject(), node.get(), depth + 1, nodeCount, seenIds);
        node->second =
            nodeFromJson(json[QLatin1String("second")].toObject(), node.get(), depth + 1, nodeCount, seenIds);
        if (!node->first || !node->second) {
            qCWarning(lcAutotile) << "SplitTree::fromJson: invalid internal node (missing child)";
            return nullptr;
        }
    } else {
        // Leaf node
        node->windowId = json[QLatin1String("windowId")].toString();
        if (node->windowId.isEmpty()) {
            qCWarning(lcAutotile) << "SplitTree::fromJson: leaf with empty windowId, skipping";
            return nullptr;
        }
        if (seenIds.contains(node->windowId)) {
            qCWarning(lcAutotile) << "SplitTree::fromJson: duplicate windowId detected, rejecting tree"
                                  << "windowId=" << node->windowId;
            return nullptr;
        }
        seenIds.insert(node->windowId);
    }

    return node;
}

} // namespace PlasmaZones
