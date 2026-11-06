#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""PreToolUse guard for the hard rules of this project (CLAUDE.md, pz-build, pz-verify-live).

Claude Code passes the tool call as JSON on stdin. Exit code 2 blocks the call and shows stderr to Claude.

A Bash command is read the way the shell reads it: split into simple commands at ; & | newlines, parentheses and
backticks outside quotes, each one tokenized with its quotes removed, and the wrappers in front of a command (then,
do, !, env, xargs, timeout, nohup, sudo...) skipped. So `then pkill x` doesn't get past a rule, and a rule's word
inside a quoted string (grep 'a|sudo') doesn't trip it. Here-document bodies (commit messages) are text, not commands.

The user runs PlasmaZones on the desktop this session runs on. The host-session rules keep a command from reaching
the user's own daemon, compositor or shell: the nested-KWin harness (scripts/nested-kwin, pz-verify-live) is where
Claude drives a PlasmaZones session.

Tests: python3 .claude/hooks/test_guard.py
"""

import json
import os
import re
import shlex
import subprocess
import sys

# Local state that never goes into git: Python caches, build trees, ctest output, audit ledgers, agent worktrees
LOCAL_PATH = re.compile(
    r'(^|/)(__pycache__|build[^/]*|\.cache|Testing|scratchpad|\.moon/cache)(/|$)'
    r'|(^|/)\.claude/(worktrees|audit-|final-report|settings\.local|scheduled_tasks)'
)
# Trees that CMake, CPM, ctest or moon write: a change goes into the source that makes them. A worktree under
# .claude/worktrees/ has build trees of its own.
BUILD_TREE = re.compile(r'^(\.claude/worktrees/[^/]+/)?(build[^/]*|\.cache|Testing|\.moon/cache)/')

SEPARATORS = ';&|()\n`'
# Words that run the command after them: skipped, with their options, to find the command itself
WRAPPERS = set(
    'then do else elif if while until ! { } time nohup env exec command builtin xargs timeout nice ionice stdbuf setsid '
    'unbuffer sudo doas dbus-run-session'.split()
)
# Wrapper options that take the next word as their value
WRAPPER_VALUES = {
    'env': {'-C', '--chdir', '-u', '--unset', '-S', '--split-string'},
    'dbus-run-session': {'--config-file', '--dbus-daemon'},
    'timeout': {'-s', '--signal', '-k', '--kill-after'},
    'nice': {'-n', '--adjustment'},
    'ionice': {'-c', '--class', '-n', '--classdata', '-p', '--pid'},
    'stdbuf': {'-i', '--input', '-o', '--output', '-e', '--error'},
    'xargs': {'-I', '--replace', '-d', '--delimiter', '-a', '--arg-file', '-E', '-n', '--max-args', '-P', '-s'},
    'exec': {'-a'},
    'time': {'-f', '--format', '-o', '--output'},
}
# A directory the guard cannot know before the shell runs: $(...), $VAR, a backquote command, cd -, GIT_DIR
UNKNOWN = object()

DBUS_TOOLS = {'qdbus', 'qdbus6', 'qdbus-qt6', 'qdbus-qt5', 'busctl', 'dbus-send', 'gdbus'}
# The root of every PlasmaZones bus name: the daemon's placement API, the settings app and the editor.
DAEMON_BUS_NAMES = ('org.phosphor_works',)
# systemctl verbs that stop, restart or disable a unit
SYSTEMCTL_DISRUPT = {
    'stop', 'restart', 'try-restart', 'reload', 'reload-or-restart', 'try-reload-or-restart', 'kill', 'disable',
    'mask', 'isolate',
}
DESKTOP_UNIT = re.compile(r'plasma|kwin|plasmazones', re.I)

MSG_LOCAL = (
    '__pycache__/, build*/, .cache/, Testing/, scratchpad/, .moon/cache/, .claude/worktrees/ and the .claude/audit-* '
    'ledgers are local state and never go into git, and `git add -f` would bypass .gitignore. Stage only source '
    'files, by name.'
)
MSG_BROAD = 'Stage files by name (git add path...), not with -A, -u, ., *, :/ or commit -a.'
MSG_PUSH = 'Never force-push. Fix the branch with a new commit instead.'
MSG_PUSH_MAIN = ('Never push to main. Push a branch and open a pull request. The user merges it. '
                 'If this command makes the branch, name it: git push -u origin BRANCH.')
MSG_PUSH_UNKNOWN = ('This push names no branch, and the command runs git in a directory the guard cannot resolve. '
                    'Name the branch: git push -u origin BRANCH.')
MSG_PKILL = (
    "pkill and killall match by name, and the user runs PlasmaZones on this desktop: `pkill plasmazonesd` or "
    "`pkill kwin` takes down the user's real session, and the pattern can match the shell that runs it. Use "
    '`pgrep -a NAME`, then `kill PID` on a build-tree or nested-session process only. scripts/nested-kwin/daemon.sh '
    'restarts the nested daemon by its pid file.'
)
MSG_PGREP_KILL = (
    'kill with pgrep -f matches the shell that runs the command, because its command line holds the pattern, and '
    "it can match the user's own plasmazonesd or KWin. List the processes with `pgrep -a NAME`. Then run `kill PID` "
    'with the numbers in a separate command.'
)
MSG_SUDO = 'Do not use sudo. The user installs system packages. Tell the user what you need and why.'
MSG_INSTALL = (
    'Never install the build (cmake --install, the install or uninstall target, make install/post-install, moon run '
    'repo:install/repo:post-install). The user installs PlasmaZones. Only a staged install into a temporary directory '
    '(--prefix /tmp/... or DESTDIR=/tmp/...) is allowed.'
)
MSG_HOST_DBUS = (
    "This D-Bus call reaches PlasmaZones (org.phosphor_works.*) on the user's own session bus, which drives the daemon of their real "
    'desktop. Run it in the nested session: source its env first (`. "$XDG_RUNTIME_DIR/pz-nested/env.sh"; qdbus6 '
    '...`, see pz-verify-live), or give the bus address explicitly. If the user asked about their live desktop, ask '
    'them to run the command with `! <command>`.'
)
MSG_DESKTOP = (
    "This command restarts or stops a process of the user's real desktop (KWin, plasmashell, plasmazonesd or their "
    'systemd units). Never do that from a session. Use the nested-KWin harness (scripts/nested-kwin, '
    'pz-verify-live), or ask the user to run it with `! <command>`.'
)


def strip_heredocs(command):
    """The command with the bodies of its here-documents left out."""
    out, pos = [], 0
    for m in re.finditer(r'<<-?[ \t]*([\'"]?)([A-Za-z_][A-Za-z0-9_]*)\1', command):
        if m.start() < pos:
            continue
        body = command.find('\n', m.end())
        if body < 0:
            break
        end = re.compile(r'^[ \t]*%s[ \t]*$' % re.escape(m.group(2)), re.M).search(command, body + 1)
        if not end:
            break
        out.append(command[pos : body + 1])
        pos = end.end()
    out.append(command[pos:])
    return ''.join(out)


def tokens(command):
    lex = shlex.shlex(command, posix=True, punctuation_chars=SEPARATORS)
    lex.whitespace = ' \t\r'
    lex.whitespace_split = True
    lex.commenters = ''
    try:
        return list(lex)
    except ValueError:  # unbalanced quotes: split at whitespace and separators, quotes dropped
        return re.findall(r'[%s]+|[^\s%s]+' % (re.escape(SEPARATORS), re.escape(SEPARATORS)), command.replace('"', ''))


def without_substitutions(command):
    """(the command line with each $(...) and backquote command as one word, $SUBST; the bodies of those commands).
    Quotes are followed as the shell follows them: nothing is special inside single quotes or after a #."""
    out, bodies, i, quote, word_start = [], [], 0, '', True
    while i < len(command):
        ch = command[i]
        if quote == "'" or (not quote and ch == '#' and word_start):
            end = command.find("'", i) if quote else command.find('\n', i + 1)
            end = len(command) if end < 0 else end + (quote == "'")
            if quote:  # a comment is left out: it is not a command, and its apostrophes are not quotes
                out.append(command[i:end])
            i, quote, word_start = end, '', False
            continue
        if ch == '\\':
            if command[i + 1 : i + 2] != '\n':  # a backslash before a newline joins the lines
                out.append(command[i : i + 2])
            i, word_start = i + 2, False
            continue
        word_start = not quote and ch in ' \t\n;&|('
        if ch in '\'"' and quote in ('', ch) and not (ch == "'" and quote):
            quote = '' if quote else ch
        if command.startswith('$(', i):
            depth, j = 1, i + 2
            while j < len(command) and depth:
                depth += {'(': 1, ')': -1}.get(command[j], 0)
                j += 1
            if not depth:
                out.append('$SUBST')
                bodies.append(command[i + 2 : j - 1])
                i = j
                continue
        elif ch == '`':
            j = command.find('`', i + 1)
            if j >= 0:
                out.append('$SUBST')
                bodies.append(command[i + 1 : j])
                i = j + 1
                continue
        out.append(ch)
        i += 1
    return ''.join(out), bodies


def simple_commands(command):
    """(wrappers, command word, arguments) for every simple command in a command line, including the ones in a
    command substitution. A substitution inside single quotes or a comment is text, as in the shell."""
    command = strip_heredocs(command)
    lines, pending = [], [command]
    while pending:
        line, bodies = without_substitutions(pending.pop())
        lines.append(line)
        pending += bodies
    for line in lines:
        words = []
        for tok in tokens(line) + [';']:
            if tok and all(ch in SEPARATORS for ch in tok):
                if words:
                    yield split_wrappers(words)
                words = []
            else:
                words.append(tok)


def split_wrappers(words):
    wrappers, i = [], 0
    while i < len(words):
        w = words[i]
        name = os.path.basename(w)
        if re.match(r'^[A-Za-z_][A-Za-z0-9_]*=', w):  # VAR=value before a command
            if w.startswith(('GIT_DIR=', 'GIT_WORK_TREE=')):
                wrappers.append('chdir:$')  # git then runs elsewhere
            wrappers.append('set:' + w)
            i += 1
        elif name in WRAPPERS:
            wrappers.append(name)
            i += 1
            # the wrapper's options with their values, and timeout's duration
            while i < len(words) and (words[i].startswith('-') or re.match(r'^[\d.]+[smhd]?$', words[i])):
                option = words[i]
                takes = option in WRAPPER_VALUES.get(name, ())
                if name == 'env' and option in ('-i', '-', '--ignore-environment'):
                    wrappers.append('clear')
                if name == 'env' and option in ('-u', '--unset') and i + 1 < len(words):
                    wrappers.append('unset:' + words[i + 1])
                elif name == 'env' and option.startswith('--unset='):
                    wrappers.append('unset:' + option[len('--unset='):])
                elif name == 'env' and option.startswith('-u') and len(option) > 2:
                    wrappers.append('unset:' + option[2:])
                if name == 'env' and option in ('-C', '--chdir') and i + 1 < len(words):
                    wrappers.append('chdir:' + words[i + 1])
                elif name == 'env' and option.startswith('--chdir='):
                    wrappers.append('chdir:' + option[len('--chdir='):])
                elif name == 'env' and option.startswith('-C') and len(option) > 2:
                    wrappers.append('chdir:' + option[2:])
                i += 2 if takes else 1
        else:
            return wrappers, name, words[i + 1 :]
    return wrappers, '', []


def short_flags(arg):
    return arg[1:] if re.match(r'^-[A-Za-z]+$', arg) else ''


def git_subcommand(args):
    """The git subcommand and its arguments, after git's own options (-C dir, -c key=value...)."""
    i = 0
    while i < len(args) and args[i].startswith('-'):
        i += 1 + (args[i] in ('-C', '-c', '--git-dir', '--work-tree', '--namespace', '--config-env'))
    return (args[i], args[i + 1 :]) if i < len(args) else ('', [])


def resolve(base, target):
    """Where `cd TARGET` from BASE lands, or UNKNOWN when the shell decides (expansions, cd -)."""
    if base is UNKNOWN or '$' in target or '`' in target or target == '-':
        return UNKNOWN
    path = os.path.abspath(os.path.join(base, os.path.expanduser(target)))
    # A cd that fails leaves the shell where it was, which is kept; a directory that mkdir made earlier in the
    # command belongs to the repository of its nearest existing parent.
    while path != '/' and not os.path.isdir(path):
        path = os.path.dirname(path)
    return path


def git_directory(args, cwd):
    """The directory git runs in: @p cwd, then its own -C options (they stack, like cd)."""
    directory, i = cwd, 0
    while i < len(args) and args[i].startswith('-'):
        if args[i] == '-C' and i + 1 < len(args):
            directory = resolve(directory, args[i + 1])
        elif args[i].startswith(('--git-dir', '--work-tree')):
            directory = UNKNOWN
        i += 1 + (args[i] in ('-C', '-c', '--git-dir', '--work-tree', '--namespace', '--config-env'))
    return directory


def check_git(args, places):
    sub, rest = git_subcommand(args)
    if sub in ('add', 'stage'):
        paths, broad, force = [], False, False
        for k, a in enumerate(rest):
            if a == '--':
                paths += rest[k + 1 :]
                break
            if a.startswith('-'):
                force |= a == '--force' or 'f' in short_flags(a)
                broad |= a in ('--all', '--update', '--no-ignore-removal') or a.startswith('--pathspec-from-file')
                broad |= 'A' in short_flags(a) or 'u' in short_flags(a)
            else:
                paths.append(a)
        if force or any(LOCAL_PATH.search(p) for p in paths):
            return MSG_LOCAL
        if broad or any(p in ('.', './', '..', '*', ':', ':/') or p.startswith((':/', ':(')) for p in paths):
            return MSG_BROAD
    elif sub == 'commit':
        k = 0
        while k < len(rest) and rest[k] != '--':
            a = rest[k]
            k += 1
            if a == '--all':
                return MSG_BROAD
            for n, ch in enumerate(short_flags(a)):
                if ch == 'a':
                    return MSG_BROAD
                if ch in 'mFCct':  # takes a value: the rest of the word, or the next word
                    k += n == len(a) - 2
                    break
    elif sub == 'push':
        for a in rest:
            if a.startswith(('--force', '--mirror')) or 'f' in short_flags(a) or a.startswith('+'):
                return MSG_PUSH
        return pushes_main(rest, [git_directory(args, place) for place in places])
    return None


def current_branch(directory):
    try:
        out = subprocess.run(['git', 'symbolic-ref', '--short', '-q', 'HEAD'], capture_output=True, text=True,
                             timeout=5, cwd=directory)
        return out.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ''


def pushes_main(rest, directories):
    """The refusal for `git push ARGS`, if it can update main: a refspec whose destination is main, --all, or no
    refspec (push.default then pushes the current branch) while main is checked out in any directory the command
    may run git in. A directory the guard cannot resolve counts as main for HEAD and the current branch."""

    def current_is_main():
        if any(d is UNKNOWN for d in directories):
            return MSG_PUSH_UNKNOWN
        return MSG_PUSH_MAIN if any(current_branch(d) == 'main' for d in directories) else None

    words, k = [], 0
    while k < len(rest):
        a = rest[k]
        k += 1
        if a == '--all' or a == '--branches':
            return MSG_PUSH_MAIN
        if a in ('-o', '--push-option', '--repo', '--receive-pack', '--exec'):
            k += 1
        elif not a.startswith('-'):
            words.append(a)
    refspecs = words[1:]
    if not refspecs:
        return current_is_main()
    for spec in refspecs:
        dst = spec.split(':', 1)[1] if ':' in spec else spec
        if dst in ('HEAD', '@') or '$' in dst:  # $(git branch --show-current) is the current branch
            if message := current_is_main():
                return message
        elif dst.removeprefix('refs/').removeprefix('heads/') == 'main':
            return MSG_PUSH_MAIN
    return None


def under_tmp(path):
    """An absolute path, written out, under /tmp. A relative path, ~ or $VAR is resolved by the shell, in a place
    the guard does not know."""
    return path.startswith('/') and os.path.realpath(path).startswith('/tmp/')


def temp_destination(command, args):
    """An install into a temporary directory (--prefix or DESTDIR under /tmp) is allowed: it never touches the system."""
    prefix = None
    for k, a in enumerate(args):
        if a == '--prefix' and k + 1 < len(args):
            prefix = args[k + 1]
        elif a.startswith('--prefix='):
            prefix = a.split('=', 1)[1]
    if prefix is not None:
        return under_tmp(prefix)
    match = re.search(r'(?:^|\s)DESTDIR=(\S+)', command)
    return bool(match) and under_tmp(match.group(1).strip('\'"'))


def is_install(word, args, command=''):
    """Whether a command installs into, or removes from, the system. post-install refreshes the user's KDE
    service cache against what is installed, so it belongs to the install step too."""
    if word == 'cmake':
        if '--install' in args:
            return not temp_destination(command, args)
        targets = [args[k + 1] for k, a in enumerate(args[:-1]) if a in ('--target', '-t')]
        targets += [a.split('=', 1)[1] for a in args if a.startswith('--target=')]
        if 'uninstall' in targets:
            return True
        return 'install' in targets and not temp_destination(command, args)
    if word in ('ninja', 'make'):
        if 'uninstall' in args or 'post-install' in args:
            return True
        return 'install' in args and not temp_destination(command, args)
    if word == 'moon':
        tasks = [a for a in args if not a.startswith('-')]
        if any(t.endswith(':post-install') for t in tasks):
            return True
        return any(t.endswith(':install') for t in tasks) and not temp_destination(command, args)
    return False


def nested_bus(word, args):
    """Whether this simple command points the commands after it at a bus other than the user's: it sources the
    nested session's env.sh, or exports DBUS_SESSION_BUS_ADDRESS."""
    if word in ('.', 'source') and args and os.path.basename(args[0]) == 'env.sh':
        return True
    if word == 'export' and any(a.startswith('DBUS_SESSION_BUS_ADDRESS=') for a in args):
        return True
    return False


def own_bus(wrappers, word, args):
    """Whether one D-Bus tool call names its own bus: a VAR= or env assignment, dbus-run-session, or an address
    option."""
    if 'dbus-run-session' in wrappers or any(w.startswith('set:DBUS_SESSION_BUS_ADDRESS=') for w in wrappers):
        return True
    if word == 'busctl' and ('--system' in args or any(a.startswith('--address') for a in args)):
        return True
    if word == 'dbus-send' and any(a in ('--system',) or a.startswith('--address=') for a in args):
        return True
    if word == 'gdbus' and any(a in ('--system', '-y', '--address', '-a') or a.startswith('--address=')
                               for a in args):
        return True
    return False


def disrupts_desktop(word, args):
    if word in ('kwin_wayland', 'kwin_x11', 'plasmashell') and '--replace' in args:
        return True
    if word in ('kquitapp6', 'kquitapp5'):
        return True
    if word == 'systemctl' and '--user' in args:
        verbs = [a for a in args if not a.startswith('-')]
        if verbs and verbs[0] in SYSTEMCTL_DISRUPT and any(DESKTOP_UNIT.search(u) for u in verbs[1:]):
            return True
    return False


def check_bash(command, cwd=None, nested=False):
    cmds = list(simple_commands(command))
    # Every directory a later git may run in: the start, each cd, pushd and env -C target, and UNKNOWN after a
    # GIT_DIR or GIT_WORK_TREE. A cd in a subshell or a failed cd leaves the start in place, so all of them are
    # kept, not only the last. A nested shell starts from all of them.
    places = list(cwd) if isinstance(cwd, list) else [cwd or os.getcwd()]
    for wrappers, word, args in cmds:
        targets = [w[len('chdir:'):] for w in wrappers if w.startswith('chdir:')]
        if word in ('cd', 'pushd'):
            operands = [a for a in args if not a.startswith('-') or a == '-']
            targets += operands[:1] or ['~']
        if word == 'export' and any(a.startswith(('GIT_DIR=', 'GIT_WORK_TREE=')) for a in args):
            targets.append('$')
        places += [resolve(place, target) for target in targets for place in list(places)]
        places = list(dict.fromkeys(places))
        if len(places) > 16:  # many cds: the guard stops guessing
            places = places[:16] + [UNKNOWN]
        nested = nested or nested_bus(word, args)
        # After this command's own env -C, GIT_DIR or bus, which the nested shell inherits
        script = None
        if word in ('bash', 'sh', 'zsh'):
            flag = next((k for k, a in enumerate(args) if a == '-c' or 'c' in short_flags(a)), None)
            script = args[flag + 1] if flag is not None and flag + 1 < len(args) else None
        elif word == 'eval' and args:
            script = ' '.join(args)
        inner_nested = nested or 'dbus-run-session' in wrappers or any(
            w.startswith('set:DBUS_SESSION_BUS_ADDRESS=') for w in wrappers)
        if script is not None and (message := check_bash(script, list(places), inner_nested)):
            return message
        if word in ('pkill', 'killall'):
            return MSG_PKILL
        if word in ('sudo', 'doas') or 'sudo' in wrappers or 'doas' in wrappers:
            return MSG_SUDO
        if is_install(word, args, command):
            return MSG_INSTALL
        if word in DBUS_TOOLS and any(name in a for a in args for name in DAEMON_BUS_NAMES) and not (
                nested or own_bus(wrappers, word, args)):
            return MSG_HOST_DBUS
        if disrupts_desktop(word, args) and not inner_nested:
            return MSG_DESKTOP
        if word == 'git':
            msg = check_git(args, places)
            if msg:
                return msg
    # kill $(pgrep -f ...), pgrep -f ... | xargs kill, a loop over pgrep -f
    pgrep_f = any(w == 'pgrep' and any(a == '--full' or 'f' in short_flags(a) for a in args) for _, w, args in cmds)
    if pgrep_f and any(w == 'kill' for _, w, _ in cmds):
        return MSG_PGREP_KILL
    return None


def check_edit(path):
    real = os.path.realpath(path)
    project = os.path.realpath(os.environ.get('CLAUDE_PROJECT_DIR', os.getcwd())) + '/'
    if not real.startswith(project):
        return None
    rel = real[len(project) :]
    if BUILD_TREE.match(rel):
        return f'{rel} is in a build tree. Change the source or CMake code that makes it.'
    return None


def main():
    call = json.load(sys.stdin)
    tool = call.get('tool_name', '')
    params = call.get('tool_input', {})
    message = None
    if tool == 'Bash':
        message = check_bash(params.get('command', ''), call.get('cwd'))
    elif tool in ('Edit', 'Write', 'MultiEdit', 'NotebookEdit'):
        message = check_edit(params.get('file_path') or params.get('notebook_path') or '')
    if message:
        print(message, file=sys.stderr)
        sys.exit(2)


if __name__ == '__main__':
    main()
