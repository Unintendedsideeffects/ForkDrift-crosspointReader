import sys
import argparse
from typing import List, Tuple

def classify_path(path: str) -> Tuple[bool, bool, bool]:
    """Returns (firmware, simulator, delivery) for a single path."""
    path = path.strip()
    if not path:
        return False, False, False

    if path in ('.github/workflows/ci.yml', '.github/workflows/build.yml'):
        return True, True, True

    if path.startswith('test/epubs/'):
        return False, True, False

    if path.startswith('docs/') or path.startswith('plans/') or path.startswith('test/'):
        return False, False, False
    if path.startswith('.github/'):
        return False, False, False
    if path in ('README.md', 'README', 'BUGS.md', 'BUGS', 'TODO.md', 'TODO'):
        return False, False, False

    if path.startswith('tools/screen-harness/'):
        return False, True, False

    if path.startswith('src/') or path.startswith('lib/') or path.startswith('include/') or \
       path.startswith('open-x4-sdk/') or path.startswith('scripts/'):
        return True, True, True
    if path in ('platformio.ini', 'partitions.csv', 'pyproject.toml', 'uv.lock', '.python-version'):
        return True, True, True

    # Rule 6: fails safe
    return True, True, True

def classify_changes(paths: List[str], force_all: bool = False) -> Tuple[bool, bool, bool]:
    if force_all or not paths:
        return True, True, True

    firmware, simulator, delivery = False, False, False
    for p in paths:
        f, s, d = classify_path(p)
        firmware = firmware or f
        simulator = simulator or s
        delivery = delivery or d
    return firmware, simulator, delivery

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--force-all', action='store_true')
    parser.add_argument('--github-output')
    args = parser.parse_args()

    lines = [line.strip() for line in sys.stdin.readlines() if line.strip()]
    f, s, d = classify_changes(lines, args.force_all)

    out_lines = [
        f"firmware={'true' if f else 'false'}",
        f"simulator={'true' if s else 'false'}",
        f"delivery={'true' if d else 'false'}",
    ]
    if args.github_output:
        with open(args.github_output, 'a') as out_f:
            for line in out_lines:
                out_f.write(line + '\n')
    else:
        for line in out_lines:
            print(line)

if __name__ == '__main__':
    main()
