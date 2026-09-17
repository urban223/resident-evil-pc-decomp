"""Check that the RAID level grammar says the same thing in all four places.

A .lvl directive lives in four places that nothing else ties together:

  src/game/RaidLevel.cpp          kRaidDirectives - what the game READS
  src/game/RaidLevel.cpp          the THE FORMAT comment at the top
  src/game/editor/EditorSave.cpp  the ED_PUT lines - what the editor WRITES
  src/game/editor/EditorSave.cpp  ED_HEADER - the comment every saved file opens with

A keyword added to one and forgotten in another fails silently: the editor
writes a line the game skips, or the game reads a directive no saved file
carries. This fails loudly instead. For every keyword it checks that each place
has it, and with the same number of integers. `ver` is written and documented
but deliberately not read, and is allowed for.

Usage:  python tests/check_raid_level_grammar.py
Exit code 0 = consistent, 1 = mismatches.
"""
import re
import sys

LOADER = 'src/game/RaidLevel.cpp'
WRITER = 'src/game/editor/EditorSave.cpp'

# written and documented, never read - see the comment above kRaidDirectives
IGNORED = {'ver'}

ROW = re.compile(r'\{\s*"([a-z]+)"\s*,\s*(\d+)\s*,\s*\w+\s*\}')
MAX_ARGS = re.compile(r'#define\s+RAID_MAX_ARGS\s+(\d+)')
# `//   box     <x0> <y0> ...` in the loader, `"#   box     <x0> ..."` in the writer
LOADER_DOC = re.compile(r'^//\s{2,}([a-z]+)\s+((?:<\w+>\s*)+)', re.M)
WRITER_DOC = re.compile(r'^"#\s{2,}([a-z]+)\s+((?:<\w+>\s*)+)', re.M)
# ED_PUT("box %6d %6d ...", ...) - the keyword is the format's first word
WRITE = re.compile(r'ED_PUT\(\s*"([a-z]+)\s([^"]*)"')
CONVERSION = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?[hl]*[diuxXs]')


def read(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()


def doc_table(rx, text):
    return {m.group(1): len(re.findall(r'<\w+>', m.group(2)))
            for m in rx.finditer(text)}


def main():
    loader = read(LOADER)
    writer = read(WRITER)

    table = {m.group(1): int(m.group(2)) for m in ROW.finditer(loader)}
    problems = []
    if not table:
        problems.append('%s: no kRaidDirectives rows found' % LOADER)

    m = MAX_ARGS.search(loader)
    max_args = int(m.group(1)) if m else 0
    for key, n in sorted(table.items()):
        if n > max_args:
            problems.append('%s: "%s" takes %d integers, RAID_MAX_ARGS is %d'
                            % (LOADER, key, n, max_args))

    written = {}
    for m in WRITE.finditer(writer):
        written[m.group(1)] = len(CONVERSION.findall(m.group(2)))

    places = (
        ('%s grammar comment' % LOADER, doc_table(LOADER_DOC, loader)),
        ('%s writer' % WRITER, written),
        ('%s ED_HEADER' % WRITER, doc_table(WRITER_DOC, writer)),
    )

    for where, found in places:
        for key, n in sorted(table.items()):
            if key not in found:
                problems.append('%s: no "%s" (the loader reads it, %d integers)'
                                % (where, key, n))
            elif found[key] != n:
                problems.append('%s: "%s" has %d integers, the loader reads %d'
                                % (where, key, found[key], n))
        for key in sorted(set(found) - set(table) - IGNORED):
            problems.append('%s: "%s" is not a directive the loader reads'
                            % (where, key))

    if problems:
        print('RAID level grammar out of step:')
        for p in problems:
            print('  ' + p)
        return 1
    print('RAID level grammar OK: %d directives agree across loader, writer '
          'and both comments.' % len(table))
    return 0


if __name__ == '__main__':
    sys.exit(main())
