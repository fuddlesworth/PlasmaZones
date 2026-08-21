// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QQuickItem>
#include <QTest>

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

using namespace PhosphorRendering;

namespace {

/// Exposes the protected registration hook so a test can stand in for the
/// scene graph, which never runs headless (updatePaintNode is only called with
/// a live QQuickWindow on a compositor).
class TrackingEffect : public ShaderEffect
{
public:
    using ShaderEffect::registerRenderNode;
};

} // namespace

/**
 * @brief Item/render-node teardown contract for ShaderEffect + ShaderNodeRhi.
 *
 * The scene graph owns render nodes and deletes them on the render thread
 * without telling the item, so the two objects can die in either order. Both
 * orders must leave the survivor with no pointer to the corpse:
 *
 *   • item first  → the node's m_item back-pointer must be severed, or the
 *                   next prepare() dereferences a freed QQuickItem;
 *   • node first  → the item's tracking must go dead, or a later teardown
 *                   calls invalidateItem() on a freed node.
 *
 * The first order is a regression guard. ~ShaderEffect used to sever only when
 * `window()` was non-null, treating a detached item as proof that the scene
 * graph had already deleted the node. It is not: an item reparented out of its
 * window and then destroyed left a live node holding a dangling back-pointer,
 * which the render thread walked on the next frame (SIGSEGV in
 * QQuickWindow::rhi(), reached from ShaderNodeRhi::prepare()).
 */
class TestShaderNodeTeardown : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// The regression: a windowless item must still sever its node's
    /// back-pointer. Headless items never have a window, so this is exactly
    /// the state the old `node && window()` guard skipped on.
    void testTeardown_severesBackPointerWhenItemHasNoWindow()
    {
        auto* effect = new TrackingEffect;
        QVERIFY(effect->window() == nullptr);

        // Stand in for the scene graph: hand the item a node the way
        // updatePaintNode would, then keep the node alive past the item.
        auto node = std::make_unique<ShaderNodeRhi>(effect);
        effect->registerRenderNode(node.get());
        QVERIFY(node->hasValidItem());

        delete effect;

        // Before the fix this stayed true and node->m_item dangled.
        QVERIFY(!node->hasValidItem());
    }

    /// A node destroyed before its item must retract itself, so the item's
    /// later teardown does not call into freed memory. Without the node's
    /// half of the contract the item would still hold a raw pointer here.
    void testTeardown_nodeDeletedFirstLeavesNoStaleTracking()
    {
        auto effect = std::make_unique<TrackingEffect>();

        auto* node = new ShaderNodeRhi(effect.get());
        effect->registerRenderNode(node);
        delete node;

        // The item is now tracking a node the "scene graph" deleted. Its
        // destructor must notice and skip the sever rather than walking the
        // freed node. Reaching the end of this test without a crash (or an
        // ASAN use-after-free) is the assertion.
        effect.reset();
        QVERIFY(true);
    }

    /// Re-registering the same node every frame is the documented contract for
    /// subclasses that reimplement updatePaintNode. It must stay idempotent —
    /// and must not disarm the teardown it exists to keep armed.
    void testTeardown_repeatedRegistrationStaysArmed()
    {
        auto* effect = new TrackingEffect;
        auto node = std::make_unique<ShaderNodeRhi>(effect);

        for (int frame = 0; frame < 8; ++frame) {
            effect->registerRenderNode(node.get());
        }
        QVERIFY(node->hasValidItem());

        delete effect;
        QVERIFY(!node->hasValidItem());
    }

    /// Explicit deregistration (updatePaintNode's zero-size branch) must leave
    /// the item with nothing to sever, so a node the item no longer owns is
    /// never touched by its destructor.
    void testTeardown_deregisteredNodeIsLeftAlone()
    {
        auto* effect = new TrackingEffect;
        auto node = std::make_unique<ShaderNodeRhi>(effect);

        effect->registerRenderNode(node.get());
        effect->registerRenderNode(nullptr);

        delete effect;

        // Still valid: the item disowned this node, so it had no business
        // invalidating it. (The zero-size branch severs explicitly, before
        // deregistering, because it deletes the node itself.)
        QVERIFY(node->hasValidItem());
    }
};

QTEST_MAIN(TestShaderNodeTeardown)
#include "test_shader_node_teardown.moc"
