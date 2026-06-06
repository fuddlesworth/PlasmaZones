// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Service.UPower
import Phosphor.Shell
import QtQuick

// Top-level composer. Owns the shared state and data sources, then
// wires them into the panel/popup/window components living in
// sibling .qml files. Sibling components are auto-discovered by Qt
// from this file's directory.
//
// Phase 2.1 PipeWire surface is registered process-globally by
// src/shell/main.cpp (Phosphor.Service.PipeWire 1.0). It is NOT
// imported here because the example shell is intentionally minimal
// (panel + clock + battery + workspace switcher). The dedicated
// CLI example `examples/phosphor-service-pipewire-cli/` is the
// shipped acceptance test for the PipeWire library; a per-app
// volume tray panel-widget that consumes PwSinkModel / PipeWireHost
// will land alongside the sibling SNI / UPower panel widgets in
// the Phase 4 panel-widgets fan-out.
Item {
    id: root

    // System data sources: owned at the top level so multiple
    // panels/windows can share a single Process / FileView each.

    // Wrap in a closure rather than assigning the bare method reference:
    // a bare `panelPopupHost.toggle` loses its `this` binding at the
    // call site, so any future maintenance that makes toggle() depend
    // on `this` (instead of lexically-captured ids) would silently
    // break. The closure binds the receiver explicitly.
    //
    // Null-guard panelPopupHost: a hot-reload race or a partial
    // sibling-component teardown could leave the id unresolved at the
    // moment the closure fires (the host might already be torn down,
    // or a consumer could invoke togglePopup before the panel is
    // instantiated on a future restructure). Silent no-op beats a
    // TypeError that breaks every subsequent panel interaction.
    Component.onCompleted: shellRouter.togglePopup = function (kind) {
        if (panelPopupHost)
            panelPopupHost.toggle(kind);
    }

    // Single source of truth for battery presence + a finite percentage.
    // Hoisted onto root so both batteryPercent and batteryVisible bind
    // the same predicate (avoids drift if one is updated without the
    // other). Number.isFinite catches the case where UPower exposes a
    // displayDevice but its percentage is NaN (transient bus-init or a
    // device disconnect mid-poll): Math.round(NaN) returns NaN, which
    // .toString() yields "NaN" rather than "". The finite check rejects
    // it so batteryPercent falls through to "" and the panel hides.
    readonly property bool batteryAvailable: !!battery.displayDevice && Number.isFinite(battery.displayDevice.percentage)

    // Persistent UI state that survives hot-reload. The persistence
    // path serialises typed POD properties only (PersistentProperties
    // silently drops anything that isn't bool/int/string/list/map/date,
    // see PersistentProperties.cpp), so a `property var togglePopup`
    // alongside the bools would be a foot-gun: it would look like part
    // of the persistent set but never get saved across hot-reloads.
    // The router below holds non-persistent fields (the togglePopup
    // function ref) and proxies the rest through to `shellState`. From
    // a consumer's perspective both shell-wide state and the router
    // surface the same name (`shellState`), via the alias declared
    // below.
    PersistentProperties {
        id: shellStatePersistent

        property bool menuOpen: false
        property bool settingsOpen: false
        property bool calendarOpen: false
        property bool mediaOpen: false
        property int activeWorkspace: 0

        reloadId: "main"
    }

    QtObject {
        id: shellRouter

        // Property aliases pass reads AND writes straight through to
        // the underlying PersistentProperties storage. No binding-
        // break-on-write hazard (which a `property bool x: persistent.x`
        // initial binding would carry).
        property alias menuOpen: shellStatePersistent.menuOpen
        property alias settingsOpen: shellStatePersistent.settingsOpen
        property alias calendarOpen: shellStatePersistent.calendarOpen
        property alias mediaOpen: shellStatePersistent.mediaOpen
        property alias activeWorkspace: shellStatePersistent.activeWorkspace
        // Function reference assigned in Component.onCompleted. TopPanel
        // and widget MouseAreas call this to toggle panel popups; the
        // host (PanelPopupHost) keeps a per-kind PanelPopup instance and
        // arbitrates ownership across them so popup-to-popup transitions
        // don't race the Wayland grab handoff. Function refs are NOT
        // persisted (PersistentProperties drops non-POD types on save),
        // which is why togglePopup lives on the router rather than
        // alongside the bool/int state.
        //
        // Default-initialised to a no-op closure so any consumer call
        // arriving before Component.onCompleted runs (a binding fires
        // during construction, or a hot-reload race) is harmless rather
        // than a TypeError on an undefined function ref. The real
        // closure (line below in Component.onCompleted) overwrites this
        // once panelPopupHost is constructed.
        property var togglePopup: function (kind) {}
    }

    // Shape: { menuOpen: bool, settingsOpen: bool, calendarOpen: bool,
    //         mediaOpen: bool, activeWorkspace: int,
    //         togglePopup: function(string) }
    // QML cannot enforce a structural contract on an aliased QtObject,
    // so consumers (TopPanel, PanelPopupHost, widgets) MUST read only
    // the fields listed above. New fields belong in shellRouter, not
    // bolted onto this alias.
    property alias shellState: shellRouter

    // Root-level alias to the TopPanel id below. Lets PanelPopupHost's
    // `topPanel` binding be written as `root.topPanelRef` (qualified),
    // mirroring the `shellState: root.shellState` pattern documented at
    // the TopPanel block: an unqualified `topPanel: topPanel` binding
    // resolves the RHS in the assignee's scope, picking up
    // PanelPopupHost's own (initially undefined) `topPanel` required
    // property instead of the outer `id: topPanel`. Using a root alias
    // forces resolution against `root`, avoiding the shadowing hazard.
    //
    // Forward reference: the `topPanel` id resolves to the TopPanel
    // block declared further down in this file. QML aliases are
    // resolved at component-completion (after the whole tree is
    // parsed), so a forward declaration like this is well-defined.
    // Kept here next to `shellState`/`shellRouter` so the public
    // surface a child component reads from `root.` stays grouped.
    readonly property alias topPanelRef: topPanel

    // Build the panel's clock+date string. Extracted from the TopPanel
    // binding below so the formatting logic is grep-able and a
    // debugger can place a breakpoint on the assembly itself.
    //
    // Defensive floor on hours >= 0: SystemClock's constructor
    // populates hours/minutes synchronously via update(), so the
    // pre-tick sentinels are never observable from QML in practice.
    // This guard only triggers if QTime::currentDateTime() ever
    // returns invalid, which it does not for any valid system time.
    // Kept as defensive belt-and-braces against future SystemClock
    // refactors that could defer the initial update().
    //
    // Qt.formatTime can't be used here because SystemClock exposes
    // only a QDate (clock.date), not a QDateTime; passing a QDate to
    // Qt.formatTime returns an empty string. String.padStart handles
    // the zero-fill cleanly.
    function buildClockText() {
        if (clock.hours < 0)
            return "";
        const hh = String(clock.hours).padStart(2, "0");
        const mm = String(clock.minutes).padStart(2, "0");
        const date = Qt.formatDate(clock.date, Qt.locale().dateFormat(Locale.ShortFormat));
        return hh + ":" + mm + " · " + date;
    }

    SystemClock {
        id: clock

        precision: SystemClock.Minutes
    }

    // CPU + memory readouts via /proc: avoids a `sh -c` subprocess
    // every 2-5s (which over a session is hundreds of fork/exec
    // pairs). A FileView re-reads the kernel-exported file in-process
    // at the interval; onContentChanged parses and deltas the values.
    FileView {
        id: cpuStat

        // Cumulative jiffies from the last snapshot (idle + total) for
        // delta computation between intervals. `real` (double): `int`
        // is 32-bit signed in QML, and on a multi-core box at 100Hz the
        // total jiffies cross INT32_MAX in days, after which assignment
        // truncates and the next delta is bogus.
        property real prevIdle: 0
        property real prevTotal: 0
        // Computed % usage, exposed as a string for the panel binding.
        // Deliberate boundary choice: format the integer-% as text at
        // the producer side so TopPanel's required `cpuPercent` prop
        // stays a string consumer-side (no Number→string coercion or
        // locale-format duplication across each panel consumer). The
        // panel renders the string directly.
        property string percent: "0"

        path: "/proc/stat"
        interval: 2000
        onContentChanged: {
            // First line: "cpu  user nice system idle iowait irq softirq steal guest guest_nice"
            const line = content.split('\n')[0];
            if (!line.startsWith('cpu '))
                return;

            const fields = line.trim().split(/\s+/).slice(1).map(s => {
                return parseInt(s, 10);
            });
            // Defensive: a kernel that doesn't expose the expected layout
            // (exotic arch, namespaced /proc) leaves NaN in `fields[3]`,
            // which propagates and parks `prevTotal` at NaN forever.
            // Warn loudly so the cause is visible in the QML log rather
            // than the panel silently freezing at the last known value.
            if (fields.length < 4 || !Number.isFinite(fields[3])) {
                console.warn("cpuStat: unexpected /proc/stat layout, skipping update (fields.length=" + fields.length + ", fields[3]=" + fields[3] + ")");
                return;
            }

            // idle = idle + iowait (matches the original awk formula).
            const idle = fields[3] + (fields[4] || 0);
            let total = 0;
            // Mirrors the fields[3] guard above: any non-finite jiffy
            // field means the kernel /proc/stat layout is broken, and a
            // silent skip would parse a partial total and produce a
            // drifting percent. Warn and early-return for visibility.
            for (const f of fields) {
                if (!Number.isFinite(f)) {
                    console.warn("cpuStat: non-finite jiffy field encountered, skipping update (field=" + f + ")");
                    return;
                }
                total += f;
            }
            if (!Number.isFinite(idle) || !Number.isFinite(total))
                return;

            if (prevTotal > 0) {
                const dTotal = total - prevTotal;
                const dIdle = idle - prevIdle;
                // dTotal <= 0 silently re-baselines on counter resets /
                // wraps (kernel jiffy counter rollover, namespace reset,
                // or suspend/resume snapshots that go backwards). The
                // next valid interval recomputes a fresh delta.
                if (dTotal > 0)
                    percent = Math.max(0, Math.min(100, Math.round((1 - dIdle / dTotal) * 100))).toString();
            }
            prevIdle = idle;
            prevTotal = total;
        }
    }

    FileView {
        id: memInfo

        // String-typed for the same reason as cpuStat.percent above:
        // text-format-at-producer keeps TopPanel.memPercent's contract
        // a string and avoids per-consumer formatting drift.
        property string percent: "0"

        path: "/proc/meminfo"
        interval: 5000
        onContentChanged: {
            let total = 0;
            let available = 0;
            let foundAvailable = false;
            for (const line of content.split('\n')) {
                // Trim leading whitespace first: a containerised or
                // future-kernel /proc/meminfo line with a leading space
                // would make split(/\s+/)[0] empty and [1] the label
                // ("MemTotal:"), which parseInt would silently turn
                // into NaN and the Number.isFinite guard below would
                // skip the update with no visible cause.
                const trimmed = line.trim();
                // Strip the leading "Label:\s*" with a single regex so
                // we are not coupled to a specific column ordering. A
                // future /proc/meminfo emitter that adds or removes a
                // whitespace column would break a positional [1]
                // lookup; the regex tolerates any inner spacing.
                if (trimmed.startsWith('MemTotal:')) {
                    total = parseInt(trimmed.replace(/^\w+:\s*/, '').split(/\s+/)[0], 10);
                } else if (trimmed.startsWith('MemAvailable:')) {
                    available = parseInt(trimmed.replace(/^\w+:\s*/, '').split(/\s+/)[0], 10);
                    foundAvailable = true;
                    break;
                }
            }
            // Require `foundAvailable`: on a kernel without MemAvailable
            // (kernel < 3.14, exotic embedded build) or a malformed
            // /proc/meminfo, available would stay at 0 and the guard
            // below would yield a bogus 100% reading.
            if (foundAvailable && Number.isFinite(total) && Number.isFinite(available) && total > 0)
                percent = Math.max(0, Math.min(100, Math.round((1 - available / total) * 100))).toString();
        }
    }

    // Battery via UPower D-Bus: replaces the raw sysfs FileView.
    // UPowerHost connects to org.freedesktop.UPower on the system bus;
    // displayDevice is the aggregate battery (percentage, state, icon).
    UPowerHost {
        id: battery
    }

    // Lock-before-sleep integration lives in SessionLockCoordinator.qml (wiring
    // SessionHost (2.10) + LockService (2.9)). It is deliberately NOT instantiated
    // in this minimal example: constructing SessionHost takes a logind block
    // inhibitor on the power / suspend / hibernate / lid keys, so mounting it
    // without the Phase 4 lock screen would grab those keys from logind with
    // nothing to handle them. The real shell mounts the coordinator once the lock
    // surface exists; the component still builds here (it is in the module's
    // QML_FILES) so the wiring stays type-checked.

    FileView {
        id: hostnameFile

        // No `interval` set: hostname is sampled once at startup only.
        // /etc/hostname rarely changes on a running session; the
        // SettingsWindow consumer re-reads on next shell launch. If
        // live hostname updates become a requirement, set
        // interval: 60000 (or similar) to poll.
        path: "/etc/hostname"
    }

    // ─── Panels ──────────────────────────────────────────────────────────
    // Single continuous top panel with left/center/right anchored zones.
    // One layer-shell surface, one exclusive zone, one shader.
    TopPanel {
        id: topPanel

        // Qualified with `root.` so QML's binding scope resolution
        // doesn't shadow the alias with TopPanel's own (initially
        // undefined) `shellState` required property. Unqualified
        // `shellState: shellState` resolves to the property being
        // assigned, leaving downstream bindings reading from an
        // undefined value.
        shellState: root.shellState
        // Time as locale-neutral HH:mm; date via Qt.formatDate using
        // the locale's short format so day/month ordering follows the
        // user's locale (the hand-rolled English arrays and hardcoded
        // "ddd MMM dd" this replaced were both i18n regressions).
        //
        // Binding reads clock.hours, clock.minutes, and clock.date via
        // buildClockText() (each a Q_PROPERTY with its own NOTIFY), so
        // it re-evaluates every minute when timeChanged fires AND on
        // day rollover when dateChanged fires. The formatting logic is
        // extracted to root.buildClockText() above for readability and
        // breakpoint placement; see that function for the floor-on-
        // hours and Qt.formatTime caveats.
        //
        // Width caveat: Qt.locale().dateFormat(Locale.ShortFormat) is
        // locale-driven and includes the year on most locales (e.g.
        // "M/d/yy", "dd/MM/yyyy"). The panel's clock cell must
        // accommodate the longest plausible string the user's locale
        // produces; do NOT assume a fixed width here. If a no-year
        // format is ever required, switch to an explicit format string
        // rather than parsing the locale's pattern.
        clockText: root.buildClockText()
        cpuPercent: cpuStat.percent
        memPercent: memInfo.percent
        // Math.round drops UPower's decimal precision. Deliberate: the
        // panel readout is a single integer-% glyph row, and a fractional
        // percentage there would be visual noise. Other panel fields
        // (CPU, memory) are already integer-rounded upstream.
        batteryPercent: root.batteryAvailable ? Math.round(battery.displayDevice.percentage).toString() : ""
        batteryVisible: root.batteryAvailable
    }

    Taskbar {}

    // ─── Popups ──────────────────────────────────────────────────────────
    // PanelPopupHost owns per-kind PanelPopup instances (calendar, media,
    // menu) and arbitrates ownership so transitions between them don't
    // race the Wayland grab handoff. See PanelPopupHost.qml.
    PanelPopupHost {
        id: panelPopupHost

        shellState: root.shellState
        // Qualified with `root.` for the same reason as `shellState`
        // above: an unqualified `topPanel: topPanel` RHS resolves in
        // PanelPopupHost's scope and picks up its own (initially
        // undefined) `topPanel` required property rather than the outer
        // `id: topPanel`. The root alias `topPanelRef` (declared near
        // `shellState`) forces resolution against `root`.
        topPanel: root.topPanelRef
    }

    // ─── Floating windows ────────────────────────────────────────────────
    SettingsWindow {
        shellState: root.shellState
        hostname: hostnameFile.content ? hostnameFile.content.trim() : ""
    }
}
