// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SplitTree.h"
#include "core/constants.h"
#include "core/logging.h"

#include <QJsonArray>
#include <QJsonObject>

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
static void splitLeaf(SplitNode* leaf, const QString& newId)
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
    leaf->splitRatio = 0.5;
    leaf->first = std::move(firstChild);
    leaf->second = std::move(secondChild);
}

void SplitTree::insertAtFocused(const QString& windowId, const QString& focusedWindowId)
{
    if (leafForWindow(windowId)) {
        qCWarning(lcAutotile) << "SplitTree: duplicate insert rejected" << windowId;
        return;
    }

    if (!m_root) {
        m_root = std::make_unique<SplitNode>();
        m_root->windowId = windowId;
        return;
    }

    SplitNode* focused = findLeaf(m_root.get(), focusedWindowId);
    if (!focused) {
        qCDebug(lcAutotile) << "insertAtFocused: focused window not found, falling back to insertAtEnd"
                            << "windowId=" << focusedWindowId;
        insertAtEnd(windowId);
        return;
    }

    splitLeaf(focused, windowId);
}

void SplitTree::insertAtEnd(const QString& windowId)
{
    if (leafForWindow(windowId)) {
        qCWarning(lcAutotile) << "SplitTree: duplicate insert rejected" << windowId;
        return;
    }

    if (!m_root) {
        m_root = std::make_unique<SplitNode>();
        m_root->windowId = windowId;
        return;
    }

    SplitNode* rm = rightmostLeaf(m_root.get());
    if (!rm) {
        qCWarning(lcAutotile) << "insertAtEnd: no rightmost leaf found (corrupt tree?)";
        return;
    }

    splitLeaf(rm, windowId);
}

void SplitTree::insertAtPosition(const QString& windowId, int position)
{
    if (leafForWindow(windowId)) {
        qCWarning(lcAutotile) << "SplitTree: duplicate insert rejected" << windowId;
        return;
    }

    if (!m_root) {
        m_root = std::make_unique<SplitNode>();
        m_root->windowId = windowId;
        return;
    }

    int currentIndex = 0;
    SplitNode* target = leafAtIndex(m_root.get(), position, currentIndex);
    if (!target) {
        insertAtEnd(windowId);
        return;
    }

    splitLeaf(target, windowId);
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
    if (!m_root) {
        return zones;
    }
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

    const qreal ratio = std::clamp(node->splitRatio, MinSplitRatio, MaxSplitRatio);

    if (node->splitHorizontal) {
        // Split top/bottom with innerGap between children
        const int contentHeight = rect.height() - innerGap;
        if (contentHeight <= 0) {
            // Gap exceeds space — give all leaves the parent rect (preserves zone count)
            applyGeometryRecursive(node->first.get(), rect, 0, zones);
            applyGeometryRecursive(node->second.get(), rect, 0, zones);
            return;
        }

        const int firstHeight = static_cast<int>(contentHeight * ratio);
        const int secondHeight = contentHeight - firstHeight;
        const QRect firstRect(rect.x(), rect.y(), rect.width(), firstHeight);
        const QRect secondRect(rect.x(), rect.y() + firstHeight + innerGap, rect.width(), secondHeight);

        if (!firstRect.isValid() || !secondRect.isValid()) {
            applyGeometryRecursive(node->first.get(), rect, 0, zones);
            applyGeometryRecursive(node->second.get(), rect, 0, zones);
            return;
        }

        applyGeometryRecursive(node->first.get(), firstRect, innerGap, zones);
        applyGeometryRecursive(node->second.get(), secondRect, innerGap, zones);
    } else {
        // Split left/right with innerGap between children
        const int contentWidth = rect.width() - innerGap;
        if (contentWidth <= 0) {
            // Gap exceeds space — give all leaves the parent rect (preserves zone count)
            applyGeometryRecursive(node->first.get(), rect, 0, zones);
            applyGeometryRecursive(node->second.get(), rect, 0, zones);
            return;
        }

        const int firstWidth = static_cast<int>(contentWidth * ratio);
        const int secondWidth = contentWidth - firstWidth;
        const QRect firstRect(rect.x(), rect.y(), firstWidth, rect.height());
        const QRect secondRect(rect.x() + firstWidth + innerGap, rect.y(), secondWidth, rect.height());

        if (!firstRect.isValid() || !secondRect.isValid()) {
            applyGeometryRecursive(node->first.get(), rect, 0, zones);
            applyGeometryRecursive(node->second.get(), rect, 0, zones);
            return;
        }

        applyGeometryRecursive(node->first.get(), firstRect, innerGap, zones);
        applyGeometryRecursive(node->second.get(), secondRect, innerGap, zones);
    }
}

// =============================================================================
// Serialization
// =============================================================================

QJsonObject SplitTree::toJson() const
{
    QJsonObject json;
    if (m_root) {
        json[QStringLiteral("root")] = nodeToJson(m_root.get());
    }
    return json;
}

std::unique_ptr<SplitTree> SplitTree::fromJson(const QJsonObject& json)
{
    auto tree = std::make_unique<SplitTree>();
    if (json.contains(QStringLiteral("root"))) {
        const QJsonObject rootObj = json[QStringLiteral("root")].toObject();
        tree->m_root = nodeFromJson(rootObj, nullptr);
    }
    return tree;
}

QJsonObject SplitTree::nodeToJson(const SplitNode* node)
{
    QJsonObject json;
    if (!node) {
        return json;
    }

    json[QStringLiteral("ratio")] = node->splitRatio;
    json[QStringLiteral("horizontal")] = node->splitHorizontal;

    if (node->isLeaf()) {
        json[QStringLiteral("windowId")] = node->windowId;
    } else {
        json[QStringLiteral("first")] = nodeToJson(node->first.get());
        json[QStringLiteral("second")] = nodeToJson(node->second.get());
    }

    return json;
}

std::unique_ptr<SplitNode> SplitTree::nodeFromJson(const QJsonObject& json, SplitNode* parent, int depth)
{
    if (json.isEmpty()) {
        return nullptr;
    }

    if (depth > MaxDeserializationDepth) {
        qCWarning(lcAutotile) << "SplitTree::fromJson: max depth exceeded, truncating";
        return nullptr;
    }

    auto node = std::make_unique<SplitNode>();
    node->parent = parent;
    node->splitRatio = json[QStringLiteral("ratio")].toDouble(0.5);
    node->splitHorizontal = json[QStringLiteral("horizontal")].toBool(false);

    if (json.contains(QStringLiteral("first")) && json.contains(QStringLiteral("second"))) {
        // Internal node
        node->first = nodeFromJson(json[QStringLiteral("first")].toObject(), node.get(), depth + 1);
        node->second = nodeFromJson(json[QStringLiteral("second")].toObject(), node.get(), depth + 1);
        if (!node->first || !node->second) {
            qCWarning(lcAutotile) << "SplitTree::fromJson: invalid internal node (missing child)";
            return nullptr;
        }
    } else {
        // Leaf node
        node->windowId = json[QStringLiteral("windowId")].toString();
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

    if (node->isLeaf()) {
        return node;
    }

    // Prefer the second (right/bottom) child for rightmost
    if (SplitNode* rm = rightmostLeaf(node->second.get())) {
        return rm;
    }
    return rightmostLeaf(node->first.get());
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

    return countLeaves(node->first.get()) + countLeaves(node->second.get());
}
