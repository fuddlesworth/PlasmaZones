// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_adaptor_scrolling_template.cpp
 * @brief LayoutAdaptor behavioral tests for the scrolling-template surface.
 *
 * Drives the real adaptor methods against a real LayoutRegistry (no bus):
 * the setScrollingTemplateLayout / getScrollingTemplateLayout pair with its
 * validation and clear forms, the mode gate on the getter, and the widened
 * {layoutId, scrollingTemplate} value shape of the three flat batch getters.
 * Fixture cribbed from test_layout_adaptor_signals.cpp.
 */

#include <QTest>

#include "dbus/layoutadaptor/layoutadaptor.h"
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>
#include <PhosphorZones/Zone.h>
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

#include <memory>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutAdaptorScrollingTemplate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_parent = new QObject(nullptr);
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), m_parent);
        m_store = std::make_unique<PhosphorZones::ScrollingTemplateStore>();
        m_layoutManager->setScrollingTemplateStore(m_store.get());
        PhosphorZones::ScrollingTemplate templ;
        templ.name = QStringLiteral("Template");
        templ.presetColumnWidths = {0.5};
        m_templateId = m_store->saveTemplate(templ).toString();
        m_adaptor = new LayoutAdaptor(m_layoutManager, m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_layoutManager = nullptr;
        m_adaptor = nullptr;
        m_store.reset();
        m_guard.reset();
    }

    void testSetGet_roundTripFlipsModeToScrolling()
    {
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), m_templateId);
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString()), m_templateId);
        QCOMPARE(m_layoutManager->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Scrolling);
    }

    void testSet_emptyIdClears()
    {
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), m_templateId);
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), QString());
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString()), QString());
        // The clear drops only the template; the context stays Scrolling.
        QCOMPARE(m_layoutManager->modeForScreen(QStringLiteral("DP-1"), 0), PhosphorZones::AssignmentEntry::Scrolling);
    }

    void testSet_unknownUuidRejected()
    {
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), m_templateId);
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(),
                                              QStringLiteral("{99999999-9999-9999-9999-999999999999}"));
        // Rejected: the stored template is unchanged.
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString()), m_templateId);
    }

    void testGet_modeGateAnswersEmptyOffScrolling()
    {
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString(), m_templateId);
        // Flip the context to Autotile: the template is preserved in the
        // entry (lossless toggle) but the GETTER routes the mode-gated
        // resolver, so it answers empty — the raw field stays readable
        // through the registry's scrollingTemplateLayoutForScreen.
        m_layoutManager->assignLayoutById(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile:bsp"));
        QCOMPARE(m_adaptor->getScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QString()), QString());
        QCOMPARE(m_layoutManager->scrollingTemplateLayoutForScreen(QStringLiteral("DP-1"), 0), m_templateId);
    }

    void testBatchGetters_carryLayoutIdAndTemplate()
    {
        // Desktop projection: a templated Scrolling context on desktop 2.
        m_layoutManager->assignLayoutById(QStringLiteral("DP-1"), 2, QString(),
                                          QString(PhosphorLayout::LayoutId::ScrollingId));
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 2, QString(), m_templateId);

        const QVariantMap desktops = m_adaptor->getAllDesktopAssignments();
        const QVariantMap desktopValue = desktops.value(QStringLiteral("DP-1|2")).toMap();
        QCOMPARE(desktopValue.value(QStringLiteral("layoutId")).toString(),
                 QString(PhosphorLayout::LayoutId::ScrollingId));
        QCOMPARE(desktopValue.value(QStringLiteral("scrollingTemplate")).toString(), m_templateId);

        // Activity projection: desktop 0 with an activity is the pure-Activity
        // context (the strict classifier keeps desktop-pinned rules out), and
        // its key is screen|activity.
        m_layoutManager->assignLayoutById(QStringLiteral("DP-1"), 0, QStringLiteral("act-y"),
                                          QString(PhosphorLayout::LayoutId::ScrollingId));
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 0, QStringLiteral("act-y"), m_templateId);

        const QVariantMap activities = m_adaptor->getAllActivityAssignments();
        const QVariantMap activityValue = activities.value(QStringLiteral("DP-1|act-y")).toMap();
        QCOMPARE(activityValue.value(QStringLiteral("layoutId")).toString(),
                 QString(PhosphorLayout::LayoutId::ScrollingId));
        QCOMPARE(activityValue.value(QStringLiteral("scrollingTemplate")).toString(), m_templateId);

        // Combined projection: same shape at the triple-pinned tuple.
        m_adaptor->setScrollingTemplateLayout(QStringLiteral("DP-1"), 3, QStringLiteral("act-x"), m_templateId);
        const QVariantMap combined = m_adaptor->getAllCombinedAssignments();
        const QVariantMap combinedValue = combined.value(QStringLiteral("DP-1|3|act-x")).toMap();
        QCOMPARE(combinedValue.value(QStringLiteral("layoutId")).toString(),
                 QString(PhosphorLayout::LayoutId::ScrollingId));
        QCOMPARE(combinedValue.value(QStringLiteral("scrollingTemplate")).toString(), m_templateId);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    std::unique_ptr<PhosphorZones::ScrollingTemplateStore> m_store;
    QObject* m_parent = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    LayoutAdaptor* m_adaptor = nullptr;
    QString m_templateId;
};

QTEST_GUILESS_MAIN(TestLayoutAdaptorScrollingTemplate)
#include "test_layout_adaptor_scrolling_template.moc"
