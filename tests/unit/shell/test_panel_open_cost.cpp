// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What it costs to OPEN a bar panel.
//
// The panels are built fresh on every click: the shell hands the popout
// transport a Component and the transport instantiates it, so everything
// the panel declares is constructed while the user is waiting for the
// first frame. A panel that builds a D-Bus service host in its own body
// therefore pays a connect and a full enumeration on the open path, and
// the surface does not appear until it finishes.
//
// This is a REGRESSION FENCE, not a benchmark: it fails when creating a
// panel crosses a budget a person would notice. The numbers here are
// deliberately loose — two orders of magnitude above what a warm creation
// costs — so that ordinary machine-to-machine variance and a loaded CI
// box cannot fail it, while the thing it exists to catch (a blocking
// construction moving onto the open path) blows through it by a wide
// margin.
//
// WHAT IT MEASURED WHEN IT WAS WRITTEN, and why that matters to the next
// person reading it: every panel creates in 0-8 ms cold and 0-1 ms warm,
// and PopoutHost — which the transport also builds per open — is under a
// millisecond. The QML side of opening a panel is free. So when a panel
// visibly fails to open smoothly, this file is the wrong place to look:
// the cost is in the transport creating a fresh Wayland layer surface per
// open and waiting on the compositor for the first frame, which no
// offscreen test can see. Written down because the obvious guess (a panel
// builds its own D-Bus service host, so THAT must be the stall) is wrong,
// and was measured to be wrong: warm creation constructs a fresh
// NetworkHost every time and still costs a millisecond.
//
// The system bus is real here, which is the point: NetworkManager, BlueZ
// and UPower are live on a developer box and their enumeration is exactly
// the cost being measured. On a machine where those daemons are absent the
// hosts go inert and the case still passes, so this never fails for the
// wrong reason.

#include <PhosphorServiceBluetooth/QmlRegistration.h>
#include <PhosphorServiceMpris/QmlRegistration.h>
#include <PhosphorServiceNetwork/QmlRegistration.h>
#include <PhosphorServicePipeWire/QmlRegistration.h>
#include <PhosphorServiceUPower/QmlRegistration.h>

#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>
#include <QUrl>

#include <memory>

class TestPanelOpenCost : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void opensWithinBudget_data();
    void opensWithinBudget();
    void popoutHostCostsLittlePerOpen();
    void contentIsPlacedBeforeItCouldBeSeen();

private:
    std::unique_ptr<QQmlEngine> m_engine;
};

namespace {
// A panel is on the click path, so this is a perceptibility budget rather
// than a performance target: past roughly this the press and the surface
// stop feeling like one event. Generous by an order of magnitude against a
// warm creation, because the failure it guards against is not "20% slower"
// but "a blocking D-Bus enumeration moved back onto the open path".
constexpr qint64 kOpenBudgetMs = 120;
} // namespace

void TestPanelOpenCost::initTestCase()
{
    // The same registrations src/shell/main.cpp makes. Without them the
    // panels' service types are unknown and every creation fails with an
    // import error rather than being measured.
    PhosphorServiceNetwork::registerQmlTypes();
    PhosphorServiceBluetooth::registerQmlTypes();
    PhosphorServiceUPower::registerQmlTypes();
    PhosphorServiceMpris::registerQmlTypes();
    PhosphorServicePipeWire::registerQmlTypes();

    m_engine = std::make_unique<QQmlEngine>();
    // The static QML modules are laid out under the build tree's qml/ root.
    m_engine->addImportPath(QStringLiteral(PZ_QML_IMPORT_DIR));

    if (qEnvironmentVariableIsSet("PZ_WARM_CHROME")) {
        // Instantiate the shared chrome once, so the per-panel numbers
        // below exclude compiling PanelFrame and its atoms. Comparing a run
        // with and without this says how much of a panel's first open is
        // its OWN type and how much is chrome every panel shares — which
        // decides whether warming one thing at startup is enough.
        QQmlComponent warm(m_engine.get());
        warm.setData("import Phosphor.Bar\nPanelFrame { }\n", QUrl(QStringLiteral("qrc:/warm.qml")));
        std::unique_ptr<QObject> chrome(warm.create());
        qInfo() << "chrome warmed:" << (chrome != nullptr);
    }
}

void TestPanelOpenCost::opensWithinBudget_data()
{
    QTest::addColumn<QString>("type");

    // Every panel a status chip can open. The calendar is left out: it
    // imports Phosphor.Shell for SystemClock, which only the shell process
    // registers, and it builds no service host so it is not what this
    // case is about.
    QTest::newRow("network") << QStringLiteral("NetworkPanel");
    QTest::newRow("bluetooth") << QStringLiteral("BluetoothPanel");
    QTest::newRow("audio") << QStringLiteral("AudioPanel");
    QTest::newRow("battery") << QStringLiteral("BatteryPanel");
    QTest::newRow("media") << QStringLiteral("MediaPanel");
}

void TestPanelOpenCost::opensWithinBudget()
{
    QFETCH(QString, type);

    const QString source = QStringLiteral("import Phosphor.Bar\n%1 { }\n").arg(type);

    QQmlComponent component(m_engine.get());
    component.setData(source.toUtf8(), QUrl(QStringLiteral("qrc:/panelopencost.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    // Twice, and both are reported, because they answer different
    // questions. The FIRST creation of any panel also compiles the
    // Phosphor.Bar types it is the first to touch (PanelFrame, PanelRow,
    // ValueRow), so a single measurement of whichever panel runs first
    // blames that one-time cost on it. The SECOND is the per-open cost,
    // paid on every click for the rest of the session, and it is the one
    // the budget is applied to.
    QElapsedTimer timer;
    timer.start();
    std::unique_ptr<QObject> cold(component.create());
    const qint64 coldMs = timer.elapsed();
    QVERIFY2(cold != nullptr, qPrintable(component.errorString()));
    cold.reset();

    timer.restart();
    std::unique_ptr<QObject> instance(component.create());
    const qint64 elapsedMs = timer.elapsed();

    QVERIFY2(instance != nullptr, qPrintable(component.errorString()));

    qInfo() << type << "cold" << coldMs << "ms, warm" << elapsedMs << "ms";
    QVERIFY2(elapsedMs < kOpenBudgetMs,
             qPrintable(QStringLiteral("%1 took %2 ms to create, budget is %3 ms. A service host or another "
                                       "blocking construction has most likely moved onto the panel's open path; "
                                       "hoist it to a process-global the shell owns and bind it instead.")
                            .arg(type)
                            .arg(elapsedMs)
                            .arg(kOpenBudgetMs)));
}

// The transport builds a PopoutHost per open (layerpopouttransport.cpp),
// so its instantiation is on the click path too, on top of the panel's.
// Measured separately because it is shared by every popout: if this is
// the expensive half, no amount of panel tuning helps.
void TestPanelOpenCost::popoutHostCostsLittlePerOpen()
{
    QQmlComponent host(m_engine.get(), QStringLiteral("Phosphor.Popout"), QStringLiteral("PopoutHost"));
    if (host.isError()) {
        QSKIP(qPrintable(QStringLiteral("Phosphor.Popout not resolvable here: %1").arg(host.errorString())));
    }

    QElapsedTimer timer;
    timer.start();
    std::unique_ptr<QObject> cold(host.create());
    const qint64 coldMs = timer.elapsed();
    QVERIFY2(cold != nullptr, qPrintable(host.errorString()));
    cold.reset();

    timer.restart();
    std::unique_ptr<QObject> instance(host.create());
    const qint64 warmMs = timer.elapsed();
    QVERIFY2(instance != nullptr, qPrintable(host.errorString()));

    qInfo() << "PopoutHost cold" << coldMs << "ms, warm" << warmMs << "ms";
    QVERIFY(warmMs < kOpenBudgetMs);
}

// The panels are reported to appear centred for a moment and then jump to
// the position under their chip. This replays what the transport does, in
// its order, and watches where the content frame actually lands.
void TestPanelOpenCost::contentIsPlacedBeforeItCouldBeSeen()
{
    QQmlComponent hostComponent(m_engine.get(), QStringLiteral("Phosphor.Popout"), QStringLiteral("PopoutHost"));
    QVERIFY2(!hostComponent.isError(), qPrintable(hostComponent.errorString()));

    QQmlComponent panelComponent(m_engine.get());
    panelComponent.setData("import Phosphor.Bar\nPanelFrame { panelWidth: 268 }\n",
                           QUrl(QStringLiteral("qrc:/placed.qml")));
    QVERIFY2(!panelComponent.isError(), qPrintable(panelComponent.errorString()));
    auto* content = qobject_cast<QQuickItem*>(panelComponent.create());
    QVERIFY(content);

    // Same sequence as LayerPopoutTransport::open: beginCreate, write every
    // property, then completeCreate.
    QObject* hostObject = hostComponent.beginCreate(m_engine->rootContext());
    auto* host = qobject_cast<QQuickItem*>(hostObject);
    QVERIFY(host);
    host->setProperty("contentItem", QVariant::fromValue(content));
    content->setParent(host);
    host->setProperty("placement", QStringLiteral("barItem"));
    host->setProperty("reservedTop", 28);
    host->setProperty("customX", 1500.0);
    hostComponent.completeCreate();

    QQuickItem* frame = content->parentItem();
    QVERIFY2(frame, "content has no visual parent; PopoutHost changed shape");

    // FAITHFUL ORDER. The transport completes creation and only then builds
    // the layer surface; the host's own size arrives when the compositor
    // configures it. So the first bindings evaluate against a 0x0 surface,
    // which is the state an earlier version of this case skipped by sizing
    // the host up front — and skipping it is what made it pass.
    const qreal firstX = frame->x();
    const qreal firstW = frame->width();
    // What the user would actually see at this instant. The transport sets
    // open=true here, before the configure, so this has to be zero.
    host->setProperty("open", true);
    const qreal opacityBeforeConfigure = frame->opacity();

    // The compositor configures the surface to the output.
    host->setWidth(1920);
    host->setHeight(1080);

    // Let layout polish run, which is what settles a ColumnLayout's implicit
    // size and therefore the frame's width.
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    const qreal settledX = frame->x();
    const qreal settledW = frame->width();

    qInfo() << "frame x" << firstX << "->" << settledX << ", width" << firstW << "->" << settledW
            << ", opacity before configure" << opacityBeforeConfigure;

    // The frame IS laid out against a 0x0 surface before the configure —
    // that is unavoidable, the size genuinely is not known yet. What must
    // not happen is the user seeing it there. So the contract under test is
    // not "it never moves", it is "it is never VISIBLE while it is in the
    // wrong place".
    QCOMPARE(opacityBeforeConfigure, 0.0);
    QVERIFY2(settledW > 0, "the frame never took a positive width");
    // The BINDING, not the animated value: opacity reaches 1 through a
    // Behavior, and a test with no render loop never ticks the animation
    // driver, so reading opacity here would report 0 forever and assert
    // nothing. `_placed` is what gates it.
    QVERIFY2(frame->property("_placed").toBool(),
             "the frame is still not considered placed after the surface was configured, so it would never fade in");

    host->deleteLater();
}

QTEST_MAIN(TestPanelOpenCost)

#include "test_panel_open_cost.moc"
