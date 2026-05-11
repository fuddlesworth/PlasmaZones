// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Phase 1 smoke test. Links the library and exercises the ShellHost
// construct / destruct path. Pins that the lib's exported symbols are
// reachable from a consumer and that the Phosphor dep chain
// (layer / shell-patterns / surfaces / animation / screens) resolves.
// Real behavioral tests land alongside the Phase 2 mechanism move.

#include <PhosphorOverlay/PhosphorOverlay.h>

#include <QObject>
#include <QtTest/QtTest>

class TestOverlaySmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void shellHostConstructsAndDestructs();
    void shellHostIsQObject();
};

void TestOverlaySmoke::shellHostConstructsAndDestructs()
{
    PhosphorOverlay::ShellHost host;
    Q_UNUSED(host);
}

void TestOverlaySmoke::shellHostIsQObject()
{
    QObject parent;
    auto* host = new PhosphorOverlay::ShellHost(&parent);
    QCOMPARE(host->parent(), &parent);
}

QTEST_MAIN(TestOverlaySmoke)
#include "test_overlay_smoke.moc"
