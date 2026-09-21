// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma Singleton
import QtQuick
import Phosphor.Shell
import Phosphor.Theme

QtObject {
    readonly property var cpu: SystemStats.snapshot.cpu || ({})
    readonly property var memory: SystemStats.snapshot.memory || ({})
    readonly property var network: SystemStats.snapshot.network || ({})
    readonly property var storage: SystemStats.snapshot.storage || ({})
    readonly property var gpus: SystemStats.snapshot.gpus || []
    readonly property var gpu: gpus.find(g => g.id === Appearance.settings.statsGpuId) || gpus[0] || ({})
    readonly property string gpuMetric: "gpu:" + (gpu.id || "")
    readonly property var drives: storage.drives || []
    readonly property var systemDrive: drives.find(d => d.mount === "/") || ({})
    readonly property var metrics: Appearance.settings.statsMetrics
    readonly property string style: Appearance.settings.statsStyle
    property Binding intervalBinding: Binding {
        target: SystemStats
        property: "interval"
        value: Appearance.settings.statsInterval * 1000
    }
    function number(value): real {
        return typeof value === "number" && isFinite(value) ? value : -1;
    }
    function percent(value): string {
        return number(value) < 0 ? "—" : Math.round(value) + "%";
    }
    function fixed(value, decimals: int): string {
        return number(value) < 0 ? "—" : Number(value).toLocaleString(Qt.locale(), "f", decimals);
    }
    function gib(value): string {
        return number(value) < 0 ? "—" : fixed(value / 1073741824, 1);
    }
    function bytes(value): string {
        if (number(value) < 0)
            return "—";
        if (value >= 1099511627776)
            return qsTr("%1 TiB").arg(fixed(value / 1099511627776, 2));
        if (value >= 1073741824)
            return qsTr("%1 GiB").arg(gib(value));
        if (value >= 1048576)
            return qsTr("%1 MiB").arg(fixed(value / 1048576, 1));
        return qsTr("%1 KiB").arg(fixed(value / 1024, 1));
    }
    function rateParts(value) {
        const scale = value >= 1000000000 ? 1000000000 : value >= 1000000 ? 1000000 : value >= 1000 ? 1000 : 1;
        return {
            value: fixed(number(value) < 0 ? -1 : value / scale, scale === 1 ? 0 : 1),
            unit: scale === 1000000000 ? qsTr("GB/s") : scale === 1000000 ? qsTr("MB/s") : scale === 1000 ? qsTr("kB/s") : qsTr("B/s")
        };
    }
    function rate(value): string {
        const parts = rateParts(value);
        return number(value) < 0 ? "—" : qsTr("%1 %2").arg(parts.value).arg(parts.unit);
    }
    function value(metric: string): real {
        return number(metric === "cpu" ? cpu.usage : metric === "gpu" ? gpu.usage : metric === "memory" ? memory.usage : metric === "network" ? network.down : systemDrive.usage);
    }
    function reading(metric: string): string {
        if (metric === "memory" && Appearance.settings.statsMemoryUnit === "used")
            return number(memory.used) < 0 ? "—" : gib(memory.used) + "G";
        if (metric !== "network")
            return percent(value(metric));
        const down = number(network.down);
        if (!network.connected)
            return qsTr("Off");
        if (down < 0)
            return "—";
        return down >= 1000000000 ? fixed(down / 1000000000, 1) + "G" : down >= 1000000 ? fixed(down / 1000000, down >= 10000000 ? 0 : 1) + "M" : down >= 1000 ? fixed(down / 1000, 0) + "k" : fixed(down, 0);
    }
    function label(metric: string): string {
        return ({
                cpu: qsTr("Processor"),
                gpu: qsTr("Graphics"),
                memory: qsTr("Memory"),
                network: qsTr("Network"),
                storage: qsTr("Storage")
            })[metric] || "";
    }
    function shortLabel(metric: string): string {
        return ({
                cpu: "CPU",
                gpu: "GPU",
                memory: "RAM",
                network: "NET",
                storage: "SSD"
            })[metric] || "";
    }
    function icon(metric: string): string {
        return ({
                cpu: "cpu",
                gpu: "video-card",
                memory: "memory",
                network: "network-transfer",
                storage: "drive-harddisk"
            })[metric] || "utilities-system-monitor";
    }
    function tone(metric: string): int {
        return ({
                cpu: 0,
                gpu: 2,
                memory: 1,
                network: 0,
                storage: 3
            })[metric] || 0;
    }
    function sensor(value, suffix: string, decimals: int): string {
        return number(value) < 0 ? qsTr("Not reported") : fixed(value, decimals) + suffix;
    }
    readonly property string uptime: {
        const seconds = number(SystemStats.snapshot.uptime);
        if (seconds < 0)
            return "—";
        const hours = Math.floor(seconds / 3600), minutes = Math.floor(seconds % 3600 / 60);
        return hours >= 24 ? qsTr("%1d %2h").arg(Math.floor(hours / 24)).arg(hours % 24) : qsTr("%1h %2m").arg(hours).arg(minutes);
    }
}
