// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Split from scriptedalgorithm.cpp to keep that translation unit under the
// project-wide 800-line cap. Holds the SplitNode → QJSValue conversion used
// by memory-aware scripts; functions must retain their original ordering.

#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/ScriptedAlgorithm.h>
#include <PhosphorTiles/SplitTree.h>
#include "tileslogging.h"

#include <QJSEngine>
#include <QJSValue>

#include <algorithm>
#include <cmath>

namespace PhosphorTiles {

QJSValue ScriptedAlgorithm::splitNodeToJSValue(const SplitNode* node, int depth) const
{
    if (!node || !m_engine || depth > MaxTreeConversionDepth) {
        return QJSValue();
    }

    // Cache Object.freeze once at the top of the recursion instead of looking it up per node
    const QJSValue freezeFn =
        m_engine->globalObject().property(QStringLiteral("Object")).property(QStringLiteral("freeze"));
    if (!freezeFn.isCallable()) {
        qCWarning(PhosphorTiles::lcTilesLib) << "Object.freeze not callable — tree will be mutable to JS scripts";
    }
    int nodeCount = 0;
    return splitNodeToJSValueImpl(node, freezeFn, depth, nodeCount);
}

QJSValue ScriptedAlgorithm::splitNodeToJSValueImpl(const SplitNode* node, const QJSValue& freezeFn, int depth,
                                                   int& nodeCount) const
{
    if (!node || !m_engine || depth > MaxTreeConversionDepth) {
        return QJSValue();
    }

    if (++nodeCount > AutotileDefaults::MaxTreeNodesForJs) {
        return QJSValue();
    }

    QJSValue jsNode = m_engine->newObject();

    if (node->isLeaf()) {
        jsNode.setProperty(QStringLiteral("windowId"), node->windowId);
    } else {
        const qreal ratio = std::isnan(node->splitRatio) ? 0.5 : std::clamp(node->splitRatio, 0.1, 0.9);
        jsNode.setProperty(QStringLiteral("ratio"), ratio);
        jsNode.setProperty(QStringLiteral("horizontal"), node->splitHorizontal);
        jsNode.setProperty(QStringLiteral("first"),
                           splitNodeToJSValueImpl(node->first.get(), freezeFn, depth + 1, nodeCount));
        jsNode.setProperty(QStringLiteral("second"),
                           splitNodeToJSValueImpl(node->second.get(), freezeFn, depth + 1, nodeCount));
    }

    // Freeze the node so scripts cannot mutate the tree representation
    if (freezeFn.isCallable()) {
        freezeFn.call({jsNode});
    }

    return jsNode;
}

} // namespace PhosphorTiles
