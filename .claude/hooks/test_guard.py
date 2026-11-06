#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cases for guard.py. Run: python3 .claude/hooks/test_guard.py"""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import guard  # noqa: E402


class BashRules(unittest.TestCase):
    def blocked(self, command, message=None, cwd=None):
        got = guard.check_bash(command, cwd or str(REPO))
        self.assertIsNotNone(got, f'not blocked: {command!r}')
        if message is not None:
            self.assertEqual(got, message, command)

    def allowed(self, command, cwd=None):
        self.assertIsNone(guard.check_bash(command, cwd or str(REPO)), command)

    def test_sudo(self):
        self.blocked('sudo pacman -S glslang', guard.MSG_SUDO)
        self.blocked('cd build && sudo make install', guard.MSG_SUDO)
        self.allowed("grep -n 'a|sudo' file")

    def test_install(self):
        for c in ('cmake --install build', 'cmake --build build --target install', 'cmake --build build -t uninstall',
                  'make install', 'make post-install', 'make uninstall', 'moon run repo:install',
                  'moon run repo:post-install', 'DESTDIR=/tmp/stage moon run repo:post-install',
                  'DESTDIR=~/stage cmake --install build'):
            self.blocked(c, guard.MSG_INSTALL)
        for c in ('cmake --install build --prefix /tmp/pz', 'DESTDIR=/tmp/stage cmake --install build',
                  'DESTDIR=/tmp/stage moon run repo:install', 'cmake --build build --parallel 6',
                  'moon run plasmazones:build', 'make test'):
            self.allowed(c)

    def test_kill(self):
        self.blocked('pkill -f plasmazonesd', guard.MSG_PKILL)
        self.blocked('then killall kwin_wayland', guard.MSG_PKILL)
        self.blocked('kill $(pgrep -f plasmazonesd)', guard.MSG_PGREP_KILL)
        self.blocked('pgrep -f kwin | xargs kill', guard.MSG_PGREP_KILL)
        self.allowed('pgrep -a plasmazonesd')
        self.allowed('kill 12345')

    def test_host_dbus(self):
        self.blocked('qdbus6 org.phosphor_works.Placement /org/phosphor_works/Placement', guard.MSG_HOST_DBUS)
        self.blocked('busctl --user call org.phosphor_works.Placement /X a b', guard.MSG_HOST_DBUS)
        self.blocked('dbus-send --session --dest=org.phosphor_works.Placement /X a.b', guard.MSG_HOST_DBUS)
        self.blocked('bash -c "qdbus6 org.phosphor_works.Placement"', guard.MSG_HOST_DBUS)
        self.allowed('. "$XDG_RUNTIME_DIR/pz-nested/env.sh"; qdbus6 org.phosphor_works.Placement '
                     '/org/phosphor_works/Placement')
        self.allowed('bash -c \'. "$XDG_RUNTIME_DIR/pz-nested/env.sh" && qdbus6 org.phosphor_works.Placement\'')
        self.allowed('DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-x qdbus6 org.phosphor_works.Placement')
        self.allowed('dbus-run-session -- qdbus6 org.phosphor_works.Placement')
        self.allowed('gdbus call --address unix:path=/tmp/b --dest org.phosphor_works.Placement -o /X -m a.b')
        self.blocked('gdbus call --session --dest org.phosphor_works.Facet -o /org/phosphor_works/Facet -m a.b',
                     guard.MSG_HOST_DBUS)
        self.allowed('qdbus6 org.kde.KWin /KWin supportInformation')
        self.allowed('grep -rn org.phosphor_works plasmazones/dbus')

    def test_desktop(self):
        self.blocked('kwin_wayland --replace &', guard.MSG_DESKTOP)
        self.blocked('plasmashell --replace', guard.MSG_DESKTOP)
        self.blocked('kquitapp6 plasmashell', guard.MSG_DESKTOP)
        self.blocked('systemctl --user restart plasmazones.service', guard.MSG_DESKTOP)
        self.blocked('systemctl --user stop plasma-kwin_wayland.service', guard.MSG_DESKTOP)
        self.allowed('systemctl --user status plasmazones.service')
        self.allowed('journalctl --user -u plasma-kwin_wayland.service -n 50')
        self.allowed('scripts/nested-kwin/run-nested.sh 2 1600 900')
        self.allowed('dbus-run-session -- bash -c "kwin_wayland --replace"')

    def test_git_add(self):
        self.blocked('git add -A', guard.MSG_BROAD)
        self.blocked('git add .', guard.MSG_BROAD)
        self.blocked('git commit -am "x"', guard.MSG_BROAD)
        self.blocked('git add -f build/foo', guard.MSG_LOCAL)
        self.blocked('git add .claude/audit-state.json', guard.MSG_LOCAL)
        self.blocked('git add Testing/Temporary/LastTest.log', guard.MSG_LOCAL)
        self.allowed('git add .claude/hooks/guard.py .claude/settings.json')
        self.allowed('git commit -m "fix: add -A handling"')
        self.allowed("git commit -F - <<'EOF'\ngit add -A\nEOF")

    def test_push(self):
        self.blocked('git push --force origin feat', guard.MSG_PUSH)
        self.blocked('git push origin +feat', guard.MSG_PUSH)
        self.blocked('git push origin main', guard.MSG_PUSH_MAIN)
        self.blocked('git push origin HEAD:refs/heads/main', guard.MSG_PUSH_MAIN)
        self.blocked('git push --all', guard.MSG_PUSH_MAIN)
        self.allowed('git push -u origin chore/claude-tooling')
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(['git', 'init', '-q', '-b', 'main', tmp], check=True)
            self.blocked('git push', guard.MSG_PUSH_MAIN, cwd=tmp)
            self.blocked('git push origin HEAD', guard.MSG_PUSH_MAIN, cwd=tmp)
            subprocess.run(['git', '-C', tmp, 'checkout', '-q', '-b', 'feat'], check=True)
            self.allowed('git push', cwd=tmp)
            self.blocked('cd "$SOMEWHERE" && git push', guard.MSG_PUSH_UNKNOWN, cwd=tmp)


class EditRules(unittest.TestCase):
    def setUp(self):
        self.saved = os.environ.get('CLAUDE_PROJECT_DIR')
        os.environ['CLAUDE_PROJECT_DIR'] = str(REPO)

    def tearDown(self):
        if self.saved is None:
            os.environ.pop('CLAUDE_PROJECT_DIR', None)
        else:
            os.environ['CLAUDE_PROJECT_DIR'] = self.saved

    def test_build_trees(self):
        for rel in ('build/CMakeCache.txt', 'build-audit/x.cpp', '.cache/cpm/luau/x.h', 'Testing/x.log',
                    '.moon/cache/x', '.claude/worktrees/pr1165/build/x.cpp'):
            self.assertIsNotNone(guard.check_edit(str(REPO / rel)), rel)
        for rel in ('plasmazones/src/daemon/daemon.cpp', '.claude/hooks/guard.py', 'cmake/PhosphorDependencies.cmake',
                    '.claude/worktrees/pr1165/plasmazones/src/x.cpp'):
            self.assertIsNone(guard.check_edit(str(REPO / rel)), rel)
        self.assertIsNone(guard.check_edit('/tmp/build/x'))


if __name__ == '__main__':
    unittest.main()
