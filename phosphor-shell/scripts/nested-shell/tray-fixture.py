#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real SNI/dbusmenu apps for an isolated session bus, with simulated actions.

Source the nested session's env.sh, then run tray-fixture.py [scenario].
For bus-only testing use dbus-run-session. No compositor is required.

Control service/interface: org.phosphor.TrayFixture
Control object: /org/phosphor/TrayFixture
Methods: Status(), Events(), ClearEvents(), SetScenario(s), SetStatus(ss),
SetRecording(b), Remove(s), Add(s), SetIconMode(ss), SetMenuProperty(sisv),
RequestMenuItem(si). Status and Events return JSON for easy assertions.
Scenarios: everyday, attention, recording, many20 (or many), few, empty.
Each app owns org.phosphor.TrayFixture.Item.<id> on its own connection and
exports /StatusNotifierItem and /Menu. Closing an item drops its connection.
The event log includes activation coordinates, scrolls, menu IDs and actions.
All actions affect this fixture only. Nothing launches or controls host apps.
"""

import argparse
from collections import deque
import json
import os
import signal
import xml.etree.ElementTree as ET

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib, GLibUnix


CONTROL = "org.phosphor.TrayFixture"
SNI = "org.kde.StatusNotifierItem"
MENU = "com.canonical.dbusmenu"
PROPERTIES = "org.freedesktop.DBus.Properties"
WATCHER = "org.kde.StatusNotifierWatcher"
SCENARIOS = ("everyday", "attention", "recording", "many20", "many", "few", "empty")
APPS = [
    ("discord", "Discord", "discord-tray", "Connected"),
    ("steam", "Steam", "steam_tray_mono", "Online"),
    ("nextcloud", "Nextcloud", "folder-sync", "Everything is up to date"),
    ("kdeconnect", "KDE Connect", "org.kde.kdeconnect", "Pixel 9 connected"),
    ("obs", "OBS Studio", "obs-tray", "Not recording"),
    ("keepass", "KeePassXC", "keepassxc-locked", "Database locked"),
    ("syncthing", "Syncthing", "folder-sync", "Up to date"),
    ("torrent", "qBittorrent", "qbittorrent-tray", "No active transfers"),
    ("signal", "Signal", "mail-message-new", "Connected"),
    ("element", "Element", "mail-message-new", "Connected"),
    ("dropbox", "Dropbox", "folder-sync", "Up to date"),
    ("copyq", "CopyQ", "edit-paste", "Clipboard ready"),
    ("flameshot", "Flameshot", "camera-photo", "Ready to capture"),
    ("kmail", "KMail", "kmail", "Inbox up to date"),
    ("telegram", "Telegram", "mail-message-new", "Connected"),
    ("mullvad", "Mullvad VPN", "network-vpn", "Connected"),
    ("updates", "Updates", "system-software-update", "System up to date"),
    ("kdewallet", "KDE Wallet", "kwalletmanager", "Wallet locked"),
    ("remmina", "Remmina", "krdc", "No active sessions"),
    ("resilio", "Resilio Sync", "folder-sync", "Up to date"),
]
APP_BY_ID = {app[0]: app for app in APPS}


def invalid(message):
    return dbus.exceptions.DBusException(
        message, name="org.freedesktop.DBus.Error.InvalidArgs")


def pixmaps(index=0, attention=False):
    """A valid network-order ARGB32 fallback; no image package is required."""
    colors = ((119, 176, 255), (99, 206, 207), (179, 150, 230), (239, 162, 151))
    color = (245, 174, 111) if attention else colors[index % len(colors)]
    result = []
    for size in (22, 44):
        pixels = bytearray()
        for y in range(size):
            for x in range(size):
                edge = min(x, y, size - x - 1, size - y - 1)
                mark = abs(x - size // 2) <= size // 12 or abs(y - size // 2) <= size // 12
                alpha = 255 if edge >= size // 5 and (edge <= size // 4 or mark) else 0
                pixels.extend((alpha, *color))
        result.append(dbus.Struct((size, size, dbus.ByteArray(pixels)), signature="iiay"))
    return dbus.Array(result, signature="(iiay)")


class PropertyObject(dbus.service.Object):
    property_types = {}

    def properties(self):
        raise NotImplementedError

    @dbus.service.method(PROPERTIES, in_signature="ss", out_signature="v")
    def Get(self, interface, name):
        props = self.GetAll(interface)
        return props[name] if name in props else self.unknown(name)

    @staticmethod
    def unknown(name):
        raise dbus.exceptions.DBusException(
            f"Unknown property {name}", name="org.freedesktop.DBus.Error.UnknownProperty")

    @dbus.service.method(PROPERTIES, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        if interface != self.interface:
            raise dbus.exceptions.DBusException(
                f"Unknown interface {interface}", name="org.freedesktop.DBus.Error.UnknownInterface")
        return dbus.Dictionary(self.properties(), signature="sv")

    @dbus.service.method(PROPERTIES, in_signature="ssv", out_signature="")
    def Set(self, interface, name, value):
        self.Get(interface, name)
        raise dbus.exceptions.DBusException(
            "Fixture properties are read-only", name="org.freedesktop.DBus.Error.PropertyReadOnly")

    @dbus.service.signal(PROPERTIES, signature="sa{sv}as")
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    @dbus.service.method("org.freedesktop.DBus.Introspectable", in_signature="", out_signature="s",
                         path_keyword="object_path", connection_keyword="connection")
    def Introspect(self, object_path, connection):
        root = ET.fromstring(super().Introspect(object_path, connection))
        interface = next((i for i in root.findall("interface") if i.get("name") == self.interface), None)
        if interface is None:
            interface = ET.SubElement(root, "interface", name=self.interface)
        for name, signature in self.property_types.items():
            ET.SubElement(interface, "property", name=name, type=signature, access="read")
        return ET.tostring(root, encoding="unicode")


def row(identifier, label="", action="", icon="", children=None, **props):
    props = {"label": label, "enabled": True, "visible": True, **props}
    if icon:
        props["icon-name"] = icon
    if children is not None:
        props["children-display"] = "submenu"
    return {"id": identifier, "props": props, "action": action, "children": children or []}


def separator(identifier):
    return row(identifier, type="separator")


class AppMenu(PropertyObject):
    interface = MENU
    property_types = {"Version": "u", "TextDirection": "s", "Status": "s", "IconThemePath": "as"}

    def __init__(self, item):
        self.item = item
        self.revision = 1
        self.projector_ready = False
        self.overrides = {}
        self.root = self.build()
        super().__init__(item.bus, "/Menu")

    def properties(self):
        return {"Version": dbus.UInt32(3), "TextDirection": "ltr", "Status": "normal",
                "IconThemePath": dbus.Array([], signature="s")}

    def build(self):
        app = self.item
        open_row = row(1, "Show OBS Studio" if app.id == "obs" else f"Open {app.title}", "open", app.icon)
        settings = row(90, "Settings…", "settings", "configure")
        quit_row = row(99, f"Exit {app.title}" if app.id in ("steam", "obs") else f"Quit {app.title}",
                       "quit", "application-exit", enabled=not app.recording)
        if app.id == "nextcloud":
            rows = [open_row, row(2, "Open sync folder", "folder", "folder"), separator(3),
                    row(10, "Resume syncing" if app.paused else "Pause syncing", "pause", "media-playback-pause"),
                    row(11, "Sync now", "sync", "view-refresh", enabled=not app.paused and app.status != "NeedsAttention")]
            if app.status == "NeedsAttention":
                rows.append(row(12, "Sign in…", "sign-in", "dialog-password"))
            rows += [separator(89), settings, quit_row]
        elif app.id == "steam":
            choices = [row(21 + i, value, "status:" + value, **{
                "toggle-type": "radio", "toggle-state": int(app.steam_status == value)})
                for i, value in enumerate(("Online", "Away", "Invisible", "Offline"))]
            rows = [open_row, row(2, "Library", "library", "applications-games"),
                    row(3, "Downloads", "downloads", "folder-download"), separator(4),
                    row(20, "Set status", children=choices), separator(89), settings, quit_row]
        elif app.id == "discord":
            mute = lambda identifier: row(identifier, "Mute notifications", "mute", **{
                "toggle-type": "checkmark", "toggle-state": int(app.muted)})
            choices = [mute(21), row(22, "Only mentions", "mentions", **{
                "toggle-type": "radio", "toggle-state": int(app.notification_scope == "mentions")}),
                row(23, "All messages", "all-messages", **{
                    "toggle-type": "radio", "toggle-state": int(app.notification_scope == "all-messages")}),
                row(24, "Notification settings…", "notification-settings", "configure")]
            rows = [open_row, separator(2), row(20, "Notifications", children=choices), mute(10),
                    separator(89), settings, quit_row]
        elif app.id == "obs":
            screens = [row(21, "Display 1", "display-1", "video-display"),
                       row(22, "Display 2", "display-2", "video-display")] if self.projector_ready else []
            rows = [open_row, separator(2), row(10, "Stop recording" if app.recording else "Start recording",
                    "record", "media-record"), row(11, "Resume recording" if app.recording_paused else "Pause recording",
                    "pause-recording", "media-playback-pause", enabled=app.recording),
                    row(20, "Fullscreen projector", children=screens), separator(89), quit_row]
        else:
            rows = [open_row]
            if app.id == "kdeconnect":
                rows += [row(2, "Ring my phone", "ping", "notifications"),
                         row(3, "Send a file…", "send", "document-send")]
            rows += [separator(89), settings, quit_row]
        root = row(0, children=rows)
        for identifier, props in self.overrides.items():
            node = self.flatten(root).get(identifier)
            if node:
                node["props"].update(props)
        return root

    @staticmethod
    def flatten(node):
        result = {node["id"]: node}
        for child in node["children"]:
            result.update(AppMenu.flatten(child))
        return result

    def node(self, identifier):
        result = self.flatten(self.root).get(int(identifier))
        if result is None:
            raise invalid(f"Unknown menu item {identifier}")
        return result

    def refresh(self):
        old = self.flatten(self.root)
        self.root = self.build()
        new = self.flatten(self.root)
        structure = lambda tree: {i: [n["id"] for n in value["children"]] for i, value in tree.items()}
        if structure(old) != structure(new):
            self.revision += 1
            self.LayoutUpdated(self.revision, 0)
            return
        changed = [(i, dbus.Dictionary(node["props"], signature="sv")) for i, node in new.items()
                   if node["props"] != old[i]["props"]]
        if changed:
            self.ItemsPropertiesUpdated(changed, [])

    @staticmethod
    def filtered(props, names):
        return dbus.Dictionary({k: v for k, v in props.items() if not names or k in names}, signature="sv")

    def layout(self, node, depth, names, variant_level=0):
        children = [] if depth == 0 else [self.layout(child, depth - 1, names, 1) for child in node["children"]]
        return dbus.Struct((dbus.Int32(node["id"]), self.filtered(node["props"], names),
                            dbus.Array(children, signature="v")), signature="ia{sv}av", variant_level=variant_level)

    @dbus.service.method(MENU, in_signature="iias", out_signature="u(ia{sv}av)")
    def GetLayout(self, parent_id, depth, property_names):
        if depth < -1:
            raise invalid("Recursion depth must be -1 or nonnegative")
        return dbus.UInt32(self.revision), self.layout(self.node(parent_id), depth, property_names)

    @dbus.service.method(MENU, in_signature="aias", out_signature="a(ia{sv})")
    def GetGroupProperties(self, identifiers, property_names):
        nodes = self.flatten(self.root)
        return [(i, self.filtered(nodes[int(i)]["props"], property_names)) for i in identifiers if int(i) in nodes]

    @dbus.service.method(MENU, in_signature="is", out_signature="v")
    def GetProperty(self, identifier, name):
        props = self.node(identifier)["props"]
        return props[name] if name in props else self.unknown(name)

    @dbus.service.method(MENU, in_signature="isvu", out_signature="")
    def Event(self, identifier, event_id, data, timestamp):
        node = self.node(identifier)
        enabled = node["props"].get("enabled", True) and node["props"].get("visible", True)
        actionable = enabled and node["props"].get("type") != "separator" and bool(node["action"])
        self.item.fixture.log("menu-event", app=self.item.id, menu=int(identifier), event=str(event_id),
                              data=data, timestamp=int(timestamp), action=node["action"], accepted=bool(actionable))
        if event_id == "clicked" and actionable:
            self.item.act(node["action"])

    @dbus.service.method(MENU, in_signature="a(isvu)", out_signature="ai")
    def EventGroup(self, events):
        errors = []
        for event in events:
            try:
                self.Event(*event)
            except dbus.exceptions.DBusException:
                errors.append(event[0])
        return errors

    @dbus.service.method(MENU, in_signature="i", out_signature="b")
    def AboutToShow(self, identifier):
        self.node(identifier)
        self.item.fixture.log("about-to-show", app=self.item.id, menu=int(identifier))
        if self.item.id == "obs" and identifier == 20 and not self.projector_ready:
            self.projector_ready = True
            self.refresh()
            return True
        return False

    @dbus.service.method(MENU, in_signature="ai", out_signature="aiai")
    def AboutToShowGroup(self, identifiers):
        updated, errors = [], []
        for identifier in identifiers:
            try:
                if self.AboutToShow(identifier):
                    updated.append(identifier)
            except dbus.exceptions.DBusException:
                errors.append(identifier)
        return updated, errors

    @dbus.service.signal(MENU, signature="a(ia{sv})a(ias)")
    def ItemsPropertiesUpdated(self, updated, removed):
        pass

    @dbus.service.signal(MENU, signature="ui")
    def LayoutUpdated(self, revision, parent):
        pass

    @dbus.service.signal(MENU, signature="iu")
    def ItemActivationRequested(self, identifier, timestamp):
        pass


class TrayItem(PropertyObject):
    interface = SNI
    property_types = {"Category": "s", "Id": "s", "Title": "s", "Status": "s", "WindowId": "u",
                      "IconThemePath": "s", "IconName": "s", "IconPixmap": "a(iiay)",
                      "OverlayIconName": "s", "OverlayIconPixmap": "a(iiay)", "AttentionIconName": "s",
                      "AttentionIconPixmap": "a(iiay)", "AttentionMovieName": "s", "ToolTip": "(sa(iiay)ss)",
                      "Menu": "o", "ItemIsMenu": "b"}

    def __init__(self, fixture, identifier):
        self.fixture = fixture
        self.id, self.title, self.icon, self.description = APP_BY_ID[identifier]
        self.bus = dbus.SessionBus(private=True)
        self.bus.set_exit_on_disconnect(False)
        self.service = f"{CONTROL}.Item.{identifier}"
        self.name = dbus.service.BusName(self.service, self.bus, do_not_queue=True)
        self.status = "Active"
        self.paused = self.muted = self.recording = self.recording_paused = False
        self.notification_scope = "mentions"
        self.steam_status = "Online"
        self.icon_mode = "theme"
        self.registered = self.registering = self.closed = False
        self.registration_generation = 0
        self.images = pixmaps(next(i for i, app in enumerate(APPS) if app[0] == identifier))
        self.attention_images = pixmaps(attention=True)
        super().__init__(self.bus, "/StatusNotifierItem")
        self.menu = AppMenu(self)
        self.last_properties = self.properties()

    def tooltip(self):
        if self.status == "NeedsAttention":
            return "Sign in to resume syncing your files." if self.id == "nextcloud" else "Needs attention"
        if self.recording:
            return "Recording paused" if self.recording_paused else "Recording · 02:18"
        if self.paused:
            return "Sync paused"
        if self.muted:
            return "Notifications muted"
        return self.steam_status if self.id == "steam" else self.description

    def properties(self):
        icon_name = ("obs-tray-active" if self.recording else self.icon) if self.icon_mode == "theme" else ""
        return {"Category": "ApplicationStatus", "Id": self.id, "Title": self.title, "Status": self.status,
                "WindowId": dbus.UInt32(0), "IconThemePath": "", "IconName": icon_name, "IconPixmap": self.images,
                "OverlayIconName": "media-record" if self.recording else "",
                "OverlayIconPixmap": self.attention_images if self.recording else dbus.Array([], signature="(iiay)"),
                "AttentionIconName": "dialog-warning" if self.icon_mode == "theme" else "",
                "AttentionIconPixmap": self.attention_images, "AttentionMovieName": "",
                "ToolTip": dbus.Struct((icon_name, self.images, self.title, self.tooltip()), signature="sa(iiay)ss"),
                "Menu": dbus.ObjectPath("/Menu"), "ItemIsMenu": self.id == "kdeconnect"}

    def register(self):
        if self.closed or self.registered or self.registering or not self.fixture.watcher_owner:
            return
        self.registering = True
        generation = self.registration_generation

        def success():
            if self.closed or generation != self.registration_generation:
                return
            self.registered, self.registering = True, False
            self.fixture.log("registered", app=self.id, service=self.service, unique=self.bus.get_unique_name())

        def failure(error):
            if self.closed or generation != self.registration_generation:
                return
            self.registering = False
            self.fixture.log("registration-failed", app=self.id, error=str(error))

        try:
            watcher = self.bus.get_object(WATCHER, "/StatusNotifierWatcher", introspect=False)
            watcher.RegisterStatusNotifierItem(self.service, dbus_interface=WATCHER,
                                               reply_handler=success, error_handler=failure)
        except dbus.exceptions.DBusException as error:
            failure(error)

    def watcher_changed(self):
        self.registration_generation += 1
        self.registered = self.registering = False
        self.register()

    def close(self):
        if self.closed:
            return
        self.closed = True
        self.menu.remove_from_connection()
        self.remove_from_connection()
        self.bus.close()

    def reset(self):
        self.status = "Active"
        self.paused = self.muted = self.recording = self.recording_paused = False
        self.notification_scope = "mentions"
        self.steam_status = "Online"
        self.icon_mode = "theme"
        self.menu.projector_ready = False
        self.menu.overrides.clear()
        self.changed()

    def changed(self):
        props = self.properties()
        changed = {key: value for key, value in props.items() if value != self.last_properties.get(key)}
        self.last_properties = props
        if "Status" in changed:
            self.NewStatus(self.status)
        if "Title" in changed:
            self.NewTitle()
        if "ToolTip" in changed:
            self.NewToolTip()
        if any(key in changed for key in ("IconName", "IconPixmap", "IconThemePath")):
            self.NewIcon()
        if any(key in changed for key in ("AttentionIconName", "AttentionIconPixmap")):
            self.NewAttentionIcon()
        if any(key in changed for key in ("OverlayIconName", "OverlayIconPixmap")):
            self.NewOverlayIcon()
        if changed:
            self.PropertiesChanged(SNI, changed, [])
        self.menu.refresh()

    def set_status(self, status):
        if status not in ("Active", "Passive", "NeedsAttention"):
            raise invalid("Status must be Active, Passive or NeedsAttention")
        if status != self.status:
            self.status = str(status)
            self.changed()

    def act(self, action):
        self.fixture.log("action", app=self.id, action=action)
        if action == "quit":
            GLib.idle_add(self.fixture.remove, self.id)
            return
        if action == "pause":
            self.paused = not self.paused
        elif action == "mute":
            self.muted = not self.muted
        elif action in ("mentions", "all-messages"):
            self.notification_scope = action
        elif action.startswith("status:"):
            self.steam_status = action.partition(":")[2]
        elif action == "sign-in":
            self.set_status("Active")
        elif action == "record":
            self.recording = not self.recording
            self.recording_paused = False
        elif action == "pause-recording":
            self.recording_paused = not self.recording_paused
        self.changed()

    @dbus.service.method(SNI, in_signature="ii", out_signature="")
    def Activate(self, x, y):
        self.fixture.log("activate", app=self.id, x=int(x), y=int(y), menuOnly=self.id == "kdeconnect")

    @dbus.service.method(SNI, in_signature="ii", out_signature="")
    def SecondaryActivate(self, x, y):
        self.fixture.log("secondary-activate", app=self.id, x=int(x), y=int(y))

    @dbus.service.method(SNI, in_signature="ii", out_signature="")
    def ContextMenu(self, x, y):
        self.fixture.log("context-menu", app=self.id, x=int(x), y=int(y))

    @dbus.service.method(SNI, in_signature="is", out_signature="")
    def Scroll(self, delta, orientation):
        if orientation not in ("vertical", "horizontal"):
            raise invalid("Scroll orientation must be vertical or horizontal")
        self.fixture.log("scroll", app=self.id, delta=int(delta), orientation=str(orientation))

    @dbus.service.signal(SNI, signature="")
    def NewTitle(self):
        pass

    @dbus.service.signal(SNI, signature="")
    def NewIcon(self):
        pass

    @dbus.service.signal(SNI, signature="")
    def NewAttentionIcon(self):
        pass

    @dbus.service.signal(SNI, signature="")
    def NewOverlayIcon(self):
        pass

    @dbus.service.signal(SNI, signature="")
    def NewToolTip(self):
        pass

    @dbus.service.signal(SNI, signature="s")
    def NewStatus(self, status):
        pass


class Fixture(dbus.service.Object):
    def __init__(self, scenario):
        self.bus = dbus.SessionBus()
        self.name = dbus.service.BusName(CONTROL, self.bus, do_not_queue=True)
        super().__init__(self.bus, "/org/phosphor/TrayFixture")
        self.events = deque(maxlen=500)
        self.sequence = 0
        self.items = {}
        self.scenario = "empty"
        self.watcher_owner = ""
        self.bus.add_signal_receiver(self.owner_changed, signal_name="NameOwnerChanged",
                                     dbus_interface="org.freedesktop.DBus", arg0=WATCHER)
        try:
            self.watcher_owner = self.bus.get_name_owner(WATCHER)
        except dbus.exceptions.DBusException:
            pass
        self.SetScenario(scenario)
        self.retry_source = GLib.timeout_add_seconds(2, self.retry_registration)

    def log(self, kind, **data):
        self.sequence += 1
        event = {"sequence": self.sequence, "kind": kind, **data}
        self.events.append(event)
        print(json.dumps(event, default=str), flush=True)

    def owner_changed(self, name, old_owner, new_owner):
        self.watcher_owner = str(new_owner)
        self.log("watcher-owner", owner=self.watcher_owner)
        for item in self.items.values():
            item.watcher_changed()

    def retry_registration(self):
        for item in self.items.values():
            item.register()
        return GLib.SOURCE_CONTINUE

    def item(self, identifier):
        if identifier not in self.items:
            raise invalid(f"No running fixture app {identifier}")
        return self.items[identifier]

    def remove(self, identifier):
        item = self.items.pop(identifier, None)
        if item:
            item.close()
            self.log("removed", app=str(identifier))
        return GLib.SOURCE_REMOVE

    @dbus.service.method(CONTROL, in_signature="", out_signature="s")
    def Status(self):
        return json.dumps({"scenario": self.scenario, "watcherOwner": self.watcher_owner,
                           "apps": [{"id": item.id, "title": item.title, "service": item.service,
                                     "unique": item.bus.get_unique_name(), "status": item.status,
                                     "tooltip": item.tooltip(), "registered": item.registered,
                                     "recording": item.recording, "menuOnly": item.id == "kdeconnect",
                                     "iconMode": item.icon_mode,
                                     "menu": [{"id": i, "action": node["action"], **node["props"]}
                                              for i, node in item.menu.flatten(item.menu.root).items()]}
                                    for item in self.items.values()]}, default=str)

    @dbus.service.method(CONTROL, in_signature="", out_signature="s")
    def Events(self):
        return json.dumps(list(self.events), default=str)

    @dbus.service.method(CONTROL, in_signature="", out_signature="")
    def ClearEvents(self):
        self.events.clear()

    @dbus.service.method(CONTROL, in_signature="s", out_signature="s")
    def SetScenario(self, scenario):
        if scenario not in SCENARIOS:
            raise invalid("Unknown scenario. Choose " + ", ".join(SCENARIOS))
        self.scenario = "many20" if scenario == "many" else str(scenario)
        count = 0 if scenario == "empty" else 2 if scenario == "few" else 20 if scenario in ("many", "many20") else 8
        desired = {app[0] for app in APPS[:count]}
        for identifier in list(self.items):
            if identifier not in desired:
                self.remove(identifier)
            else:
                self.items[identifier].reset()
        for app in APPS[:count]:
            self.Add(app[0])
        if scenario == "attention":
            self.items["nextcloud"].set_status("NeedsAttention")
        elif scenario == "recording":
            self.SetRecording(True)
        self.log("scenario", scenario=self.scenario)
        return self.Status()

    @dbus.service.method(CONTROL, in_signature="ss", out_signature="")
    def SetStatus(self, identifier, status):
        self.item(identifier).set_status(status)
        self.log("status", app=str(identifier), status=str(status))

    @dbus.service.method(CONTROL, in_signature="b", out_signature="")
    def SetRecording(self, recording):
        app = self.item("obs")
        app.recording = bool(recording)
        app.recording_paused = False
        app.changed()
        self.log("recording", recording=app.recording)

    @dbus.service.method(CONTROL, in_signature="s", out_signature="b")
    def Remove(self, identifier):
        exists = identifier in self.items
        self.remove(identifier)
        return exists

    @dbus.service.method(CONTROL, in_signature="s", out_signature="s")
    def Add(self, identifier):
        if identifier not in APP_BY_ID:
            raise invalid(f"Unknown fixture app {identifier}")
        if identifier not in self.items:
            self.items[str(identifier)] = TrayItem(self, str(identifier))
            self.items[identifier].register()
            self.log("added", app=str(identifier))
        return self.items[identifier].service

    @dbus.service.method(CONTROL, in_signature="ss", out_signature="")
    def SetIconMode(self, identifier, mode):
        if mode not in ("theme", "pixmap"):
            raise invalid("Icon mode must be theme or pixmap")
        item = self.item(identifier)
        item.icon_mode = str(mode)
        item.changed()

    @dbus.service.method(CONTROL, in_signature="sisv", out_signature="")
    def SetMenuProperty(self, identifier, menu_id, name, value):
        if name in ("enabled", "visible") and not isinstance(value, dbus.Boolean):
            raise invalid("Menu enabled and visible properties must be boolean")
        if name in ("label", "icon-name") and not isinstance(value, dbus.String):
            raise invalid("Menu label and icon-name properties must be strings")
        if name not in ("enabled", "visible", "label", "icon-name"):
            raise invalid("SetMenuProperty supports enabled, visible, label and icon-name")
        menu = self.item(identifier).menu
        menu.node(menu_id)
        menu.overrides.setdefault(int(menu_id), {})[str(name)] = value
        menu.refresh()

    @dbus.service.method(CONTROL, in_signature="si", out_signature="")
    def RequestMenuItem(self, identifier, menu_id):
        menu = self.item(identifier).menu
        menu.node(menu_id)
        menu.ItemActivationRequested(menu_id, 0)

    def close(self):
        GLib.source_remove(self.retry_source)
        for identifier in list(self.items):
            self.remove(identifier)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenario", nargs="?", choices=SCENARIOS, default="everyday")
    args = parser.parse_args()
    address = os.environ.get("DBUS_SESSION_BUS_ADDRESS", "")
    host_paths = {f"/run/user/{os.getuid()}/bus", os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "bus")}
    if not address or any(f"path={path}" in address for path in host_paths):
        parser.error("Set DBUS_SESSION_BUS_ADDRESS to the isolated harness bus, or use dbus-run-session.")
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    fixture = Fixture(args.scenario)
    loop = GLib.MainLoop()

    def stop():
        loop.quit()
        return GLib.SOURCE_REMOVE

    for sig in (signal.SIGINT, signal.SIGTERM):
        GLibUnix.signal_add(GLib.PRIORITY_DEFAULT, sig, stop)
    print(f"Tray fixture ready: {args.scenario}", flush=True)
    try:
        loop.run()
    finally:
        fixture.close()


if __name__ == "__main__":
    main()
