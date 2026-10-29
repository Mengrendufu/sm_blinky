#!/usr/bin/env python3
"""Line width check for the lines we author.

The rule is about our lines, not about the files we own: 78 display columns.
Display columns, because the comment blocks are full of CJK, which takes two
columns per character. Bytes and character counts both get that wrong.

Vendor text keeps its own shape - the ST template banners are 80 columns on
purpose, the Amazon licence wraps where it wraps - and we do not rewrap it.
When we edit such a file, only the lines we add or change are held to the
limit, which is what the default mode measures.

Usage:
  python3 tools/check_line_width.py            # lines we added/changed, plus
                                               # untracked source files whole
  python3 tools/check_line_width.py FILE ...   # those files whole

Exits 1 when something is over the limit.
"""
import os
import re
import subprocess
import sys
import unicodedata

LIMIT = 78
EXTS = ('.c', '.h')
# The trees that hold code we write. Narrowing the pathspec keeps git off the
# submodules and the build tree, which matters on this checkout (drvfs).
SOURCE_PATHS = ('app', 'bootloader', 'project_common', 'tests', 'tools',
                'application', 'bsp', 'ports')
RECORD = re.compile(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@')


def display_width(text):
    cols = 0

    for ch in text:
        if unicodedata.combining(ch):
            continue
        cols += 2 if unicodedata.east_asian_width(ch) in ('W', 'F') else 1

    return cols


def git(root, args):
    cmd = ['git', '--no-pager', '-C', root] + args
    env = dict(os.environ, GIT_OPTIONAL_LOCKS='0')
    try:
        done = subprocess.run(cmd, check=True, capture_output=True, text=True,
                              env=env)
    except FileNotFoundError:
        print('git not available: name the files to check instead')
        sys.exit(2)
    except subprocess.CalledProcessError as err:
        print('git failed: %s' % err.stderr.strip())
        sys.exit(2)

    return done.stdout


def added_lines(root):
    """Line numbers we added or rewrote, as {repo relative path: {line}}."""
    found = {}
    path = None

    cmd = ['diff', '-U0', '-M', '-C', '--find-copies-harder',
           '--no-color', 'HEAD', '--'] + list(SOURCE_PATHS)

    for line in git(root, cmd).splitlines():
        if line.startswith('+++ b/'):
            path = line[len('+++ b/'):]
            continue

        head = RECORD.match(line)
        if head is None or path is None or not path.endswith(EXTS):
            continue

        first = int(head.group(1))
        count = 1 if head.group(2) is None else int(head.group(2))
        if count > 0:
            found.setdefault(path, set()).update(range(first, first + count))

    return found


def untracked_sources(root):
    cmd = ['ls-files', '--others', '--exclude-standard', '--'] + list(SOURCE_PATHS)
    listed = git(root, cmd)
    return [p for p in listed.splitlines() if p.endswith(EXTS)]


def check(root, label, numbers):
    """numbers: line numbers to look at, or None for the whole file."""
    path = os.path.join(root, label)
    try:
        with open(path, encoding='utf-8', errors='replace') as handle:
            lines = handle.read().splitlines()
    except OSError as err:
        print('%s: cannot read: %s' % (label, err))
        return 0

    wanted = range(1, len(lines) + 1) if numbers is None else sorted(numbers)
    over = 0

    for number in wanted:
        if number > len(lines):
            continue
        width = display_width(lines[number - 1].rstrip(' \t'))
        if width > LIMIT:
            print('%s:%d: %d columns' % (label, number, width))
            over += 1

    return over


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    total = 0

    if argv:
        for path in argv:
            label = os.path.relpath(path, root) if os.path.isabs(path) else path
            total += check(root, label, None)
        looked = len(argv)
    else:
        changed = added_lines(root)
        untouched = untracked_sources(root)
        for label in sorted(changed):
            total += check(root, label, changed[label])
        for label in untouched:
            total += check(root, label, None)
        looked = len(changed) + len(untouched)

    if total:
        print('%d line(s) over %d columns' % (total, LIMIT))
        return 1

    print('%d file(s) looked at, our lines are within %d columns' %
          (looked, LIMIT))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
