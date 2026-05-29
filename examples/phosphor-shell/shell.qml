// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Service.UPower
import Phosphor.Shell
import QtQuick

// Top-level composer. Owns the shared state and data sources, then
// wires them into the panel/popup/window components living in
// sibling .qml files. Sibling components are auto-discovered by Qt
// from this file's directory.
Item {
    id: root

    // System data sources: owned at the top level so multiple
    // panels/windows can share a single Process / FileView each.

    // Wrap in a closure rather than assigning the bare method reference:
    // a bare `panelPopupHost.toggle` loses its `this` binding at the
    // call site, so any future maintenance that makes toggle() depend
    // on `this` (instead of lexically-captured ids) would silently
    // break. The closure binds the receiver explicitly.
    Component.onCompleted: shellRouter.togglePopup = function (kind) {
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
            for (const f of fields) {
                if (Number.isFinite(f))
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
                percent = Math.round((1 - available / total) * 100).toString();
        }
    }

    // Battery via UPower D-Bus: replaces the raw sysfs FileView.
    // UPowerHost connects to org.freedesktop.UPower on the system bus;
    // displayDevice is the aggregate battery (percentage, state, icon).
    UPowerHost {
        id: battery
    }

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
        // Binding reads clock.hours, clock.minutes, and clock.date
        // (each a Q_PROPERTY with its own NOTIFY), so it re-evaluates
        // every minute when timeChanged fires AND on day rollover when
        // dateChanged fires. Qt.formatTime can't be used here because
        // SystemClock exposes only a QDate (clock.date), not a
        // QDateTime; passing a QDate to Qt.formatTime returns an empty
        // string. String.padStart handles the zero-fill cleanly.
        //
        // Defensive floor on hours >= 0: SystemClock initialises its
        // hours/minutes Q_PROPERTYs to -1 before its first tick, and a
        // hot-reload can momentarily expose the pre-update state. The
        // guard yields "" rather than flashing "-1:-1 · " on the panel.
        clockText: clock.hours >= 0 ? String(clock.hours).padStart(2, "0") + ":" + String(clock.minutes).padStart(2, "0") + " · " + Qt.formatDate(clock.date, Qt.locale().dateFormat(Locale.ShortFormat)) : ""
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
        topPanel: topPanel
    }

    // ─── Floating windows ────────────────────────────────────────────────
    SettingsWindow {
        shellState: root.shellState
        hostname: hostnameFile.content ? hostnameFile.content.trim() : ""
    }
}
