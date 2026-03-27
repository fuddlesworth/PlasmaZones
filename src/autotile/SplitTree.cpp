// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SplitTree.h"
#include "core/constants.h"
#include "core/logging.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QSet>

#include <algorithm>

using namespace PlasmaZones;
using namespace PlasmaZones::AutotileDefaults;

// =============================================================================
// Construction / Move
// =============================================================================

SplitTree::SplitTree() = default;

SplitTree::SplitTree(SplitTree&& other) noexcept
    : m_root(std::move(other.m_root))
{
}

SplitTree& SplitTree::operator=(SplitTree&& other) noexcept
{
    if (this != &other) {
        m_root = std::move(other.m_root);
    }
    return *this;
}

SplitTree::~SplitTree() = default;

// =============================================================================
// Queries
// =============================================================================

SplitNode* SplitTree::root() const
{
    return m_root.get();
}

bool SplitTree::isEmpty() const
{
    return !m_root;
}

int SplitTree::leafCount() const
{
    return countLeaves(m_root.get());
}

int SplitTree::nodeDepth(const SplitNode* node)
{
    if (!node)
        return 0;
    return 1 + std::max(nodeDepth(node->first.get()), nodeDepth(node->second.get()));
}

int SplitTree::treeDepth() const
{
    return nodeDepth(m_root.get());
}

SplitNode* SplitTree::leafForWindow(const QString& windowId) const
{
    return findLeaf(m_root.get(), windowId);
}

QStringList SplitTree::leafOrder() const
{
    QStringList order;
    collectLeafOrder(m_root.get(), order);
    return order;
}

// =============================================================================
// Mutations — Insert
// =============================================================================

/**
 * @brief Split a leaf node into an internal node with two children
 *
 * The original occupant (existingId) stays in the first child.
 * The new window (newId) goes into the second child.
 * Split direction alternates from the parent's direction.
 */
void SplitTree::splitLeaf(SplitNode* leaf, const QString& newId, qreal ratio)
{
    // Choose split direction: alternate from parent, default to vertical (left/right)
    bool horizontal = false;
    if (leaf->parent) {
        horizontal = !leaf->parent->splitHorizontal;
    }

    auto firstChild = std::make_unique<SplitNode>();
    firstChild->windowId = leaf->windowId;
    firstChild->parent = leaf;

    auto secondChild = std::make_unique<SplitNode>();
    secondChild->windowId = newId;
    secondChild->parent = leaf;

    // Convert leaf to internal node
    leaf->windowId.clear();
    leaf->splitHorizontal = horizontal;
    // Use provided ratio if valid, otherwise use default
    leaf->splitRatio = (ratio > 0.0)
        ? std::clamp(ratio, PlasmaZones::AutotileDefaults::MinSplitRatio, PlasmaZones::AutotileDefaults::MaxSplitRatio)
        : PlasmaZones::AutotileDefaults::DefaultSplitRatio;
    leaf->first = std::move(firstChild);
    leaf->second = std::move(secondChild);
}

SplitTree::InsertReady SplitTree::prepareInsert(const QString& windowId)
{
    if (leafForWindow(windowId)) {
        qCWarning(lcAutotile) << "SplitTree: duplicate insert rejected" << windowId;
        return InsertReady::Rejected;
    }

    if (!m_root) {
        m_root = std::make_unique<SplitNode>();
        m_root->windowId = windowId;
        return InsertReady::Done;
    }

    // E1: Only run O(n) depth traversal when the tree is large enough to matter
    if (leafCount() > 15 && treeDepth() >= MaxRuntimeTreeDepth) {
        qCWarning(lcAutotile) << "SplitTree: max depth reached, rejecting insert";
        return InsertReady::Rejected;
    }

    return InsertReady::Proceed;
}

void SplitTree::insertAtFocused(const QString& windowId, const QString& focusedWindowId, qreal initialRatio)
{
    const auto ready = prepareInsert(windowId);
    if (ready != InsertReady::Proceed)
        return;

    SplitNode* focused = focusedWindowId.isEmpty() ? nullptr : findLeaf(m_root.get(), focusedWindowId);
    if (!focused) {
        qCDebug(lcAutotile) << "insertAtFocused: focused window not found, falling back to insertAtEnd"
                            << "windowId=" << focusedWindowId;
        insertAtEnd(windowId, initialRatio);
        return;
    }

    splitLeaf(focused, windowId, initialRatio);
}

void SplitTree::insertAtEnd(const QString& windowId, qreal initialRatio)
{
    const auto ready = prepareInsert(windowId);
    if (ready != InsertReady::Proceed)
        return;

    SplitNode* rm = rightmostLeaf(m_root.get());
    if (!rm) {
        qCWarning(lcAutotile) << "insertAtEnd: no rightmost leaf found (corrupt tree?)";
        return;
    }

    splitLeaf(rm, windowId, initialRatio);
}

void SplitTree::insertAtPosition(const QString& windowId, int position, qreal initialRatio)
{
    const auto ready = prepareInsert(windowId);
    if (ready != InsertReady::Proceed)
        return;

    int currentIndex = 0;
    SplitNode* target = leafAtIndex(m_root.get(), position, currentIndex);
    if (!target) {
        insertAtEnd(windowId, initialRatio);
        return;
    }

    splitLeaf(target, windowId, initialRatio);
}

// =============================================================================
// Mutations — Remove
// =============================================================================

void SplitTree::remove(const QString& windowId)
{
    SplitNode* leaf = findLeaf(m_root.get(), windowId);
    if (!leaf) {
        qCWarning(lcAutotile) << "remove: window not found" << "windowId=" << windowId;
        return;
    }

    // If the leaf IS the root, the tree becomes empty
    if (leaf == m_root.get()) {
        m_root.reset();
        return;
    }

    SplitNode* parent = leaf->parent;
    Q_ASSERT(parent);

    // Determine sibling (the other child of parent)
    std::unique_ptr<SplitNode> sibling;
    if (parent->first.get() == leaf) {
        sibling = std::move(parent->second);
    } else {
        sibling = std::move(parent->first);
    }

    if (parent == m_root.get()) {
        // Parent is root — sibling becomes the new root
        sibling->parent = nullptr;
        m_root = std::move(sibling);
    } else {
        // Parent has a grandparent — replace parent with sibling
        SplitNode* grandparent = parent->parent;
        sibling->parent = grandparent;

        if (grandparent->first.get() == parent) {
            grandparent->first = std::move(sibling);
        } else {
            grandparent->second = std::move(sibling);
        }
        // parent (old internal node) is destroyed here via grandparent's old unique_ptr
    }
}

// =============================================================================
// Mutations — Swap / Resize
// =============================================================================

void SplitTree::swap(const QString& windowId1, const QString& windowId2)
{
    SplitNode* leaf1 = findLeaf(m_root.get(), windowId1);
    SplitNode* leaf2 = findLeaf(m_root.get(), windowId2);

    if (!leaf1 || !leaf2) {
        qCWarning(lcAutotile) << "swap: one or both windows not found"
                              << "id1=" << windowId1 << "id2=" << windowId2;
        return;
    }

    std::swap(leaf1->windowId, leaf2->windowId);
}

void SplitTree::resizeSplit(const QString& windowId, qreal newRatio)
{
    SplitNode* leaf = findLeaf(m_root.get(), windowId);
    if (!leaf) {
        qCWarning(lcAutotile) << "resizeSplit: window not found" << "windowId=" << windowId;
        return;
    }

    if (!leaf->parent) {
        qCDebug(lcAutotile) << "resizeSplit: leaf is root, no parent split to resize";
        return;
    }

    leaf->parent->splitRatio = std::clamp(newRatio, MinSplitRatio, MaxSplitRatio);
}

// =============================================================================
// Geometry
// =============================================================================

QVector<QRect> SplitTree::applyGeometry(const QRect& area, int innerGap) const
{
    QVector<QRect> zones;
    // EC-2: Zero-size rect guard
    if (area.width() <= 0 || area.height() <= 0) {
        return zones;
    }
    if (!m_root) {
        return zones;
    }
    // EC-3: Clamp negative innerGap
    innerGap = qMax(0, innerGap);
    applyGeometryRecursive(m_root.get(), area, innerGap, zones);
    return zones;
}

void SplitTree::applyGeometryRecursive(const SplitNode* node, const QRect& rect, int innerGap,
                                       QVector<QRect>& zones) const
{
    if (!node) {
        return;
    }

    if (node->isLeaf()) {
        zones.append(rect);
        return;
    }

    if (!node->first || !node->second) {
        qCWarning(lcAutotile) << "applyGeometryRecursive: corrupt internal node (missing child)";
        return;
    }

    const qreal ratio = std::clamp(node->splitRatio, MinSplitRatio, MaxSplitRatio);

    // DRY helper: split along one dimension, building child rects via callbacks
    auto splitDimension = [&](int totalSize, auto makeFirstRect, auto makeSecondRect) {
        const int contentSize = totalSize - innerGap;
        if (contentSize <= 0) {
            // Gap exceeds space — give all leaves the parent rect (preserves zone count)
            applyGeometryRecursive(node->first.get(), rect, 0, zones);
            applyGeometryRecursive(node->second.get(), rect, 0, zones);
            return;
        }

        const int firstSize = std::max(1, static_cast<int>(contentSize * ratio));
        const int secondSize = std::max(1, contentSize - firstSize);
        const QRect firstRect = makeFirstRect(firstSize);
        const QRect secondRect = makeSecondRect(firstSize, secondSize);

        if (!firstRect.isValid() || !secondRect.isValid()) {
            applyGeometryRecursive(node->first.get(), rect, 0, zones);
            applyGeometryRecursive(node->second.get(), rect, 0, zones);
            return;
        }

        applyGeometryRecursive(node->first.get(), firstRect, innerGap, zones);
        applyGeometryRecursive(node->second.get(), secondRect, innerGap, zones);
    };

    if (node->splitHorizontal) {
        splitDimension(
            rect.height(),
            [&](int firstH) {
                return QRect(rect.x(), rect.y(), rect.width(), firstH);
            },
            [&](int firstH, int secondH) {
                const int secondY = rect.y() + firstH + innerGap;
                const int clampedH = std::max(1, std::min(secondH, rect.y() + rect.height() - secondY));
                return QRect(rect.x(), secondY, rect.width(), clampedH);
            });
    } else {
        splitDimension(
            rect.width(),
            [&](int firstW) {
                return QRect(rect.x(), rect.y(), firstW, rect.height());
            },
            [&](int firstW, int secondW) {
                const int secondX = rect.x() + firstW + innerGap;
                const int clampedW = std::max(1, std::min(secondW, rect.x() + rect.width() - secondX));
                return QRect(secondX, rect.y(), clampedW, rect.height());
            });
    }
}

// =============================================================================
// Rebuild from order (SOLID-1: moved from TilingState)
// =============================================================================

void SplitTree::rebuildFromOrder(const QStringList& tiledWindows, qreal defaultSplitRatio)
{
    if (tiledWindows.isEmpty()) {
        m_root.reset();
        return;
    }
    // Deduplicate input while preserving order, skipping empty IDs
    QStringList uniqueWindows;
    QSet<QString> seen;
    for (const auto& wid : tiledWindows) {
        if (!wid.isEmpty() && !seen.contains(wid)) {
            seen.insert(wid);
            uniqueWindows.append(wid);
        }
    }

    if (uniqueWindows.isEmpty()) {
        m_root.reset();
        return;
    }
    if (uniqueWindows.size() == 1) {
        auto singleTree = std::make_unique<SplitNode>();
        singleTree->windowId = uniqueWindows.first();
        m_root = std::move(singleTree);
        return;
    }

    // Capture existing split ratios from old tree
    const int oldLeafCount = countLeaves(m_root.get());
    QVector<qreal> oldRatios;
    QVector<bool> oldDirections;
    collectInternalNodeParams(m_root.get(), oldRatios, oldDirections);

    // Build a fresh tree from deduplicated input
    m_root.reset();
    for (const QString& windowId : uniqueWindows) {
        insertAtEnd(windowId, defaultSplitRatio);
    }

    // Restore ratios if leaf count matches
    if (oldLeafCount != leafCount()) {
        qCDebug(lcAutotile) << "rebuildFromOrder: leaf count changed from" << oldLeafCount << "to" << leafCount()
                            << "-- skipping ratio restoration";
    } else {
        applyInternalNodeParams(m_root.get(), oldRatios, oldDirections, 0);
    }
}

void SplitTree::collectInternalNodeParams(const SplitNode* node, QVector<qreal>& ratios, QVector<bool>& directions)
{
    if (!node || node->isLeaf()) {
        return;
    }
    ratios.append(node->splitRatio);
    directions.append(node->splitHorizontal);
    collectInternalNodeParams(node->first.get(), ratios, directions);
    collectInternalNodeParams(node->second.get(), ratios, directions);
}

int SplitTree::applyInternalNodeParams(SplitNode* node, const QVector<qreal>& ratios, const QVector<bool>& directions,
                                       int index)
{
    if (!node || node->isLeaf()) {
        return index;
    }
    if (index < ratios.size()) {
        node->splitRatio = ratios[index];
        node->splitHorizontal = directions[index];
    }
    ++index;
    index = applyInternalNodeParams(node->first.get(), ratios, directions, index);
    index = applyInternalNodeParams(node->second.get(), ratios, directions, index);
    return index;
}

// =============================================================================
// Serialization
// =============================================================================

QJsonObject SplitTree::toJson() const
{
    QJsonObject json;
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
    tree->m_root = nodeFromJson(rootObj, nullptr, 0, nodeCount);
    if (!tree->m_root) {
        return nullptr;
    }
    return tree;
}

QJsonObject SplitTree::nodeToJson(const SplitNode* node)
{
    QJsonObject json;
    if (!node) {
        return json;
    }

    if (node->isLeaf()) {
        json[QLatin1String("windowId")] = node->windowId;
    } else {
        json[QLatin1String("ratio")] = node->splitRatio;
        json[QLatin1String("horizontal")] = node->splitHorizontal;
        json[QLatin1String("first")] = nodeToJson(node->first.get());
        json[QLatin1String("second")] = nodeToJson(node->second.get());
    }

    return json;
}

std::unique_ptr<SplitNode> SplitTree::nodeFromJson(const QJsonObject& json, SplitNode* parent, int depth,
                                                   int& nodeCount)
{
    if (json.isEmpty()) {
        return nullptr;
    }

    if (depth > MaxDeserializationDepth) {
        qCWarning(lcAutotile) << "SplitTree::fromJson: max depth exceeded, truncating";
        return nullptr;
    }

    if (++nodeCount > MaxDeserializationNodes) {
        qCWarning(lcAutotile) << "SplitTree::fromJson: max node count exceeded, truncating";
        return nullptr;
    }

    auto node = std::make_unique<SplitNode>();
    node->parent = parent;
    node->splitRatio = std::clamp(json[QLatin1String("ratio")].toDouble(0.5), MinSplitRatio, MaxSplitRatio);
    node->splitHorizontal = json[QLatin1String("horizontal")].toBool(false);

    if (json.contains(QLatin1String("first")) && json.contains(QLatin1String("second"))) {
        // Internal node
        node->first = nodeFromJson(json[QLatin1String("first")].toObject(), node.get(), depth + 1, nodeCount);
        node->second = nodeFromJson(json[QLatin1String("second")].toObject(), node.get(), depth + 1, nodeCount);
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
    }

    return node;
}

// =============================================================================
// Private helpers
// =============================================================================

SplitNode* SplitTree::findLeaf(SplitNode* node, const QString& windowId) const
{
    if (!node) {
        return nullptr;
    }

    if (node->isLeaf()) {
        return (node->windowId == windowId) ? node : nullptr;
    }

    if (SplitNode* found = findLeaf(node->first.get(), windowId)) {
        return found;
    }
    return findLeaf(node->second.get(), windowId);
}

SplitNode* SplitTree::leafAtIndex(SplitNode* node, int targetIndex, int& currentIndex) const
{
    if (!node) {
        return nullptr;
    }

    if (node->isLeaf()) {
        if (currentIndex == targetIndex) {
            return node;
        }
        ++currentIndex;
        return nullptr;
    }

    if (SplitNode* found = leafAtIndex(node->first.get(), targetIndex, currentIndex)) {
        return found;
    }
    return leafAtIndex(node->second.get(), targetIndex, currentIndex);
}

SplitNode* SplitTree::rightmostLeaf(SplitNode* node) const
{
    if (!node) {
        return nullptr;
    }
    while (!node->isLeaf()) {
        node = node->second ? node->second.get() : node->first.get();
    }
    return node;
}

void SplitTree::collectLeafOrder(const SplitNode* node, QStringList& order) const
{
    if (!node) {
        return;
    }

    if (node->isLeaf()) {
        order.append(node->windowId);
        return;
    }

    collectLeafOrder(node->first.get(), order);
    collectLeafOrder(node->second.get(), order);
}

int SplitTree::countLeaves(const SplitNode* node) const
{
    if (!node) {
        return 0;
    }

    if (node->isLeaf()) {
        return 1;
    }

    if (!node->first || !node->second) {
        qCWarning(lcAutotile) << "countLeaves: corrupt internal node (missing child)";
        return 0;
    }

    return countLeaves(node->first.get()) + countLeaves(node->second.get());
}
