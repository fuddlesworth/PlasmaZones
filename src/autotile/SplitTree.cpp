// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SplitTree.h"
#include "core/constants.h"
#include "core/logging.h"

#include <algorithm>

namespace PlasmaZones {

using namespace AutotileDefaults;

namespace {
bool hasValidChildren(const SplitNode* node)
{
    if (!node->first || !node->second) {
        qCWarning(lcAutotile) << "Corrupt internal node: missing child";
        return false;
    }
    return true;
}
} // anonymous namespace

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

const SplitNode* SplitTree::root() const noexcept
{
    return m_root.get();
}
SplitNode* SplitTree::root() noexcept
{
    return m_root.get();
}
bool SplitTree::isEmpty() const noexcept
{
    return !m_root;
}
int SplitTree::leafCount() const noexcept
{
    return countLeaves(m_root.get());
}

int SplitTree::subtreeHeight(const SplitNode* node, int depth)
{
    if (!node || depth > MaxRuntimeTreeDepth)
        return 0;
    return 1 + std::max(subtreeHeight(node->first.get(), depth + 1), subtreeHeight(node->second.get(), depth + 1));
}

int SplitTree::treeHeight() const
{
    return subtreeHeight(m_root.get());
}

const SplitNode* SplitTree::leafForWindow(const QString& windowId) const
{
    return findLeaf(m_root.get(), windowId);
}

SplitNode* SplitTree::leafForWindow(const QString& windowId)
{
    return const_cast<SplitNode*>(std::as_const(*this).leafForWindow(windowId));
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
    Q_ASSERT(leaf);
    if (!leaf)
        return;

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
    leaf->splitRatio = (ratio > 0.0) ? std::clamp(ratio, MinSplitRatio, MaxSplitRatio) : DefaultSplitRatio;
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
    if (leafCount() > 15 && treeHeight() >= MaxRuntimeTreeDepth) {
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
        insertAtEndImpl(windowId, initialRatio);
        return;
    }

    splitLeaf(focused, windowId, initialRatio);
}

void SplitTree::insertAtEnd(const QString& windowId, qreal initialRatio)
{
    const auto ready = prepareInsert(windowId);
    if (ready != InsertReady::Proceed)
        return;

    insertAtEndImpl(windowId, initialRatio);
}

void SplitTree::insertAtEndImpl(const QString& windowId, qreal initialRatio)
{
    SplitNode* rm = rightmostLeaf(m_root.get());
    if (!rm) {
        qCWarning(lcAutotile) << "insertAtEndImpl: no rightmost leaf found (corrupt tree?)";
        return;
    }

    splitLeaf(rm, windowId, initialRatio);
}

void SplitTree::insertAtEndRaw(const QString& windowId, qreal initialRatio)
{
    if (!m_root) {
        m_root = std::make_unique<SplitNode>();
        m_root->windowId = windowId;
        return;
    }

    SplitNode* rm = rightmostLeaf(m_root.get());
    if (!rm) {
        qCWarning(lcAutotile) << "insertAtEndRaw: no rightmost leaf found (corrupt tree?)";
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
        insertAtEndImpl(windowId, initialRatio);
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
    if (windowId1 == windowId2)
        return;

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

void SplitTree::applyGeometryRecursive(const SplitNode* node, const QRect& rect, int innerGap, QVector<QRect>& zones,
                                       int depth) const
{
    if (!node || depth > MaxRuntimeTreeDepth) {
        return;
    }

    if (node->isLeaf()) {
        zones.append(rect);
        return;
    }

    if (!hasValidChildren(node)) {
        return;
    }

    const qreal ratio = std::clamp(node->splitRatio, MinSplitRatio, MaxSplitRatio);

    // DRY helper: split along one dimension, building child rects via callbacks
    auto splitDimension = [&](int totalSize, auto makeFirstRect, auto makeSecondRect) {
        const int contentSize = totalSize - innerGap;
        if (contentSize <= 0) {
            // Gap exceeds space — give all leaves the parent rect (preserves zone count)
            applyGeometryRecursive(node->first.get(), rect, 0, zones, depth + 1);
            applyGeometryRecursive(node->second.get(), rect, 0, zones, depth + 1);
            return;
        }

        const int firstSize = std::max(1, static_cast<int>(contentSize * ratio));
        const int secondSize = std::max(1, contentSize - firstSize);
        const QRect firstRect = makeFirstRect(firstSize);
        const QRect secondRect = makeSecondRect(firstSize, secondSize);

        if (!firstRect.isValid() || !secondRect.isValid()) {
            applyGeometryRecursive(node->first.get(), rect, 0, zones, depth + 1);
            applyGeometryRecursive(node->second.get(), rect, 0, zones, depth + 1);
            return;
        }

        applyGeometryRecursive(node->first.get(), firstRect, innerGap, zones, depth + 1);
        applyGeometryRecursive(node->second.get(), secondRect, innerGap, zones, depth + 1);
    };

    if (node->splitHorizontal) {
        splitDimension(
            rect.height(),
            [&](int firstH) {
                return QRect(rect.x(), rect.y(), rect.width(), firstH);
            },
            [&](int firstH, int secondH) {
                const int secondY = std::min(rect.y() + firstH + innerGap, rect.y() + rect.height() - 1);
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
                const int secondX = std::min(rect.x() + firstW + innerGap, rect.x() + rect.width() - 1);
                const int clampedW = std::max(1, std::min(secondW, rect.x() + rect.width() - secondX));
                return QRect(secondX, rect.y(), clampedW, rect.height());
            });
    }
}

// =============================================================================
// Private helpers (const versions do real work; non-const delegates safely)
// =============================================================================

const SplitNode* SplitTree::findLeaf(const SplitNode* node, const QString& windowId) const
{
    if (!node) {
        return nullptr;
    }

    if (node->isLeaf()) {
        return (node->windowId == windowId) ? node : nullptr;
    }

    if (!hasValidChildren(node)) {
        return nullptr;
    }

    if (const SplitNode* found = findLeaf(node->first.get(), windowId)) {
        return found;
    }
    return findLeaf(node->second.get(), windowId);
}

SplitNode* SplitTree::findLeaf(SplitNode* node, const QString& windowId) const
{
    return const_cast<SplitNode*>(findLeaf(static_cast<const SplitNode*>(node), windowId));
}

const SplitNode* SplitTree::leafAtIndex(const SplitNode* node, int targetIndex, int& currentIndex) const
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

    if (!hasValidChildren(node)) {
        return nullptr;
    }

    if (const SplitNode* found = leafAtIndex(node->first.get(), targetIndex, currentIndex)) {
        return found;
    }
    return leafAtIndex(node->second.get(), targetIndex, currentIndex);
}

SplitNode* SplitTree::leafAtIndex(SplitNode* node, int targetIndex, int& currentIndex) const
{
    return const_cast<SplitNode*>(leafAtIndex(static_cast<const SplitNode*>(node), targetIndex, currentIndex));
}

const SplitNode* SplitTree::rightmostLeaf(const SplitNode* node) const
{
    if (!node) {
        return nullptr;
    }
    const SplitNode* current = node;
    while (!current->isLeaf()) {
        const SplitNode* next = current->second ? current->second.get() : current->first.get();
        if (!next)
            break; // corrupt internal node -- treat current as leaf
        current = next;
    }
    return current;
}

SplitNode* SplitTree::rightmostLeaf(SplitNode* node) const
{
    return const_cast<SplitNode*>(rightmostLeaf(static_cast<const SplitNode*>(node)));
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

    if (!hasValidChildren(node)) {
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

    if (!hasValidChildren(node)) {
        return 0;
    }

    return countLeaves(node->first.get()) + countLeaves(node->second.get());
}

} // namespace PlasmaZones
