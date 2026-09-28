#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rich, actionable notifications for the isolated nested-shell harness.

Source the session env.sh, run this script, then call Seed/Rich/Burst/Replace
on org.phosphor.NotificationFixture at /org/phosphor/NotificationFixture.
Actions and replies are recorded; this fixture never launches applications.
"""
import dbus, dbus.service, dbus.mainloop.glib, json, os, sys
from gi.repository import GLib
if not os.environ.get('WAYLAND_DISPLAY', '').startswith('pznested'):
    raise SystemExit('This fixture requires the isolated nested compositor.')
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
def notification_server():
    return dbus.Interface(bus.get_object('org.freedesktop.Notifications', '/org/freedesktop/Notifications'), 'org.freedesktop.Notifications')
from pathlib import Path
image = sys.argv[1] if len(sys.argv) > 1 else str(Path(__file__).resolve().parents[2] / 'shell/wallpapers/wallpaper.png')
long_message = 'I went through the notification mockup this morning. The grouped cards are much easier to scan, especially when a chat gets busy while a build is running.\n\nA few details to try: keep the app icon visible beside the sender, let attached pictures keep their original proportions, and make the full message available without opening the app. A short preview should still leave room for Reply and Dismiss.\n\nI also attached the wallpaper study so we can check the same notification against dark glass, Paper, and Ember. The softer violet background should work with all three. Let me know what you think when you have a moment.'
events = []
observed_replies = []
observer = dbus.SessionBus(private=True)
observer.add_signal_receiver(lambda *args: observed_replies.append(list(args)), signal_name='NotificationReplied', dbus_interface='org.freedesktop.Notifications')
def event(*args, member=None):
    events.append({'signal': member, 'args': list(args)})
    print(json.dumps({'signal': member, 'args': list(args)}, default=str), flush=True)
for signal in ('NotificationClosed', 'ActionInvoked', 'NotificationReplied', 'ActivationToken'):
    bus.add_signal_receiver(event, signal_name=signal, dbus_interface='org.freedesktop.Notifications', member_keyword='member')
class Fixture(dbus.service.Object):
    def __init__(self):
        self.name = dbus.service.BusName('org.phosphor.NotificationFixture', bus)
        super().__init__(bus, '/org/phosphor/NotificationFixture')
        self.latest = 0
    def send(self, app, title, body, icon='dialog-information', actions=None, hints=None, timeout=0, replaces=0):
        result = notification_server().Notify(app, dbus.UInt32(replaces), icon, title, body, dbus.Array(actions or [], signature='s'), dbus.Dictionary(hints or {}, signature='sv'), dbus.Int32(timeout))
        self.latest = int(result)
        return result
    @dbus.service.method('org.phosphor.NotificationFixture', in_signature='', out_signature='s')
    def Status(self):
        return json.dumps({'latest': self.latest, 'events': events, 'observerReplies': observed_replies}, default=str)
    @dbus.service.method('org.phosphor.NotificationFixture', in_signature='', out_signature='u')
    def Rich(self):
        return self.send('Element', 'Maya · Shell design', long_message, 'org.kde.kdeconnect', ['default', 'Open chat', 'inline-reply', 'Reply'], {'image-path': image, 'x-kde-reply-placeholder-text': 'Reply to Maya…'})
    @dbus.service.method('org.phosphor.NotificationFixture', in_signature='', out_signature='u')
    def Seed(self):
        self.send('Konsole', 'Build finished', 'All targets built successfully in 42 seconds.', 'org.kde.konsole')
        self.send('Dolphin', 'Download complete', 'phosphor-wallpapers.zip · 24.8 MB', 'org.kde.dolphin', ['default', 'Show file'])
        self.send('Dolphin', '12 files copied', 'Design references → Pictures / Inspiration', 'org.kde.dolphin', ['default', 'Open folder'])
        self.send('Element', 'Design room', 'Maya shared a wallpaper study.', 'org.kde.kdeconnect', ['default', 'Open chat'], {'image-path': image})
        self.send('Element', 'Alex · Shell design', long_message, 'org.kde.kdeconnect', ['default', 'Open chat', 'inline-reply', 'Reply'])
        self.send('Element', 'Maya · Shell design', 'The new lock screen feels right. I left a few notes on the spacing.', 'org.kde.kdeconnect', ['default', 'Open chat', 'inline-reply', 'Reply'])
        return self.send('Backups', 'Backup drive disconnected', 'Reconnect Archive to finish your backup.', 'dialog-warning', ['default', 'View backup'], {'urgency': dbus.Byte(2)})
    @dbus.service.method('org.phosphor.NotificationFixture', in_signature='', out_signature='u')
    def Replace(self):
        return self.send('Element', 'Updated in place', 'The same ID now has new content and actions.', 'org.kde.kdeconnect', ['default', 'Open update'], replaces=self.latest)
    @dbus.service.method('org.phosphor.NotificationFixture', in_signature='i', out_signature='u')
    def Burst(self, count):
        for i in range(min(60, count)):
            self.send('Build worker', f'Build step {i+1}', 'Saved during a busy workspace.', 'org.kde.konsole', timeout=500)
        return self.latest
    @dbus.service.method('org.phosphor.NotificationFixture', in_signature='i', out_signature='u')
    def Expiring(self, timeout):
        return self.send('Element', 'Time to read', long_message, 'org.kde.kdeconnect', ['inline-reply', 'Reply'], timeout=timeout)
fixture=Fixture()
print('Notification fixture ready', flush=True)
GLib.MainLoop().run()
