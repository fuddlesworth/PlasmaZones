// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_adaptor_signals.cpp
 * @brief LayoutAdaptor signal emission contract tests.
 *
 * Pins the rule (Phase 1.2 of refactor/dbus-performance): property
 * mutations (setLayoutHidden, setLayoutAutoAssign, setLayoutAspectRatioClass)
 * emit `layoutChanged` but NEVER `layoutListChanged` — the list hasn't
 * changed. layoutListChanged is reserved for genuine add/delete/reload
 * operations (onLayoutsChanged, notifyLayoutListChanged).
 *
 * Subscribers (SettingsController) wire both signals to the same reload
 * slot, so dropping the redundant list-changed emission is behavior-
 * preserving on the client side but shaves one D-Bus marshal + slot
 * invocation per property mutation.
 */

#include <QTest>
#include <QSignalSpy>

#include "dbus/layoutadaptor.h"
#include "core/layout.h"
#include "core/layoutmanager.h"
#include "core/zone.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutAdaptorSignals : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_parent = new QObject(nullptr);
        m_layoutManager = new LayoutManager(m_parent);
        auto* layout = new Layout(QStringLiteral("SignalTestLayout"));
        for (int i = 0; i < 2; ++i) {
            auto* zone = new Zone(QRectF(0.5 * i, 0.0, 0.5, 1.0));
            zone->setZoneNumber(i + 1);
            layout->addZone(zone);
        }
        m_layoutManager->addLayout(layout);
        m_layoutId = layout->id().toString();
        m_adaptor = new LayoutAdaptor(m_layoutManager, m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_layoutManager = nullptr;
        m_adaptor = nullptr;
        m_guard.reset();
    }

    // ─────────────────────────────────────────────────────────────────
    // setLayoutHidden: property change, not a list change.
    // ─────────────────────────────────────────────────────────────────
    void testSetLayoutHidden_emitsLayoutChangedOnly()
    {
        QSignalSpy changed(m_adaptor, &LayoutAdaptor::layoutChanged);
        QSignalSpy listChanged(m_adaptor, &LayoutAdaptor::layoutListChanged);

        m_adaptor->setLayoutHidden(m_layoutId, true);

        QCOMPARE(changed.count(), 1);
        QCOMPARE(listChanged.count(), 0);
    }

    void testSetLayoutAutoAssign_emitsLayoutChangedOnly()
    {
        QSignalSpy changed(m_adaptor, &LayoutAdaptor::layoutChanged);
        QSignalSpy listChanged(m_adaptor, &LayoutAdaptor::layoutListChanged);

        m_adaptor->setLayoutAutoAssign(m_layoutId, true);

        QCOMPARE(changed.count(), 1);
        QCOMPARE(listChanged.count(), 0);
    }

    void testSetLayoutAspectRatioClass_emitsLayoutChangedOnly()
    {
        QSignalSpy changed(m_adaptor, &LayoutAdaptor::layoutChanged);
        QSignalSpy listChanged(m_adaptor, &LayoutAdaptor::layoutListChanged);

        m_adaptor->setLayoutAspectRatioClass(m_layoutId, 1);

        QCOMPARE(changed.count(), 1);
        QCOMPARE(listChanged.count(), 0);
    }

    // ─────────────────────────────────────────────────────────────────
    // notifyLayoutListChanged: the one path that SHOULD emit
    // layoutListChanged (daemon calls it after Apply-time reloads).
    // This is the inverse of the rule above — guards against
    // over-zealous removal of the emission.
    // ─────────────────────────────────────────────────────────────────
    void testNotifyLayoutListChanged_emitsListChanged()
    {
        QSignalSpy listChanged(m_adaptor, &LayoutAdaptor::layoutListChanged);

        m_adaptor->notifyLayoutListChanged();

        QCOMPARE(listChanged.count(), 1);
    }

    // ─────────────────────────────────────────────────────────────────
    // Phase 1.3: getLayout must never serve stale JSON after a property
    // mutation. Before the fix, m_cachedLayoutJson was populated on the
    // first getLayout() call but never invalidated by setLayoutHidden,
    // setLayoutAutoAssign, or setLayoutAspectRatioClass — so a second
    // getLayout() call would return the pre-mutation JSON.
    // ─────────────────────────────────────────────────────────────────
    void testGetLayout_cacheInvalidated_afterSetLayoutHidden()
    {
        // Prime the cache.
        const QString beforeJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(!beforeJson.isEmpty());
        // Layout::toJson() only serializes the hiddenFromSelector key when
        // the flag is true, so beforeJson (default false) must not contain it.
        QVERIFY(!beforeJson.contains(QLatin1String("hiddenFromSelector")));

        m_adaptor->setLayoutHidden(m_layoutId, true);

        // The next read must reflect the new value — no stale cache.
        const QString afterJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(!afterJson.isEmpty());
        QVERIFY(afterJson.contains(QLatin1String("hiddenFromSelector")));
        QVERIFY(afterJson != beforeJson);
    }

    void testGetLayout_cacheInvalidated_afterSetLayoutAutoAssign()
    {
        const QString beforeJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(!beforeJson.isEmpty());
        QVERIFY(!beforeJson.contains(QLatin1String("autoAssign")));

        m_adaptor->setLayoutAutoAssign(m_layoutId, true);

        const QString afterJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(afterJson.contains(QLatin1String("autoAssign")));
        QVERIFY(afterJson != beforeJson);
    }

    void testGetLayout_cacheInvalidated_afterSetLayoutAspectRatioClass()
    {
        const QString beforeJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(!beforeJson.isEmpty());

        m_adaptor->setLayoutAspectRatioClass(m_layoutId, 2);

        const QString afterJson = m_adaptor->getLayout(m_layoutId);
        QVERIFY(afterJson != beforeJson);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    LayoutManager* m_layoutManager = nullptr;
    LayoutAdaptor* m_adaptor = nullptr;
    QString m_layoutId;
};

QTEST_MAIN(TestLayoutAdaptorSignals)
#include "test_layout_adaptor_signals.moc"
