#!/usr/bin/env python3
"""Run alive-tv on every .ll file in the current directory, saving output to <file>.ll.log."""

import subprocess
import sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

ALIVE_TV = Path.home() / 'alive2-regehr/build/alive-tv'
FLAGS    = ['-disable-undef-input']


def run_one(ll):
    log = Path(str(ll) + '.log')
    r = subprocess.run([str(ALIVE_TV)] + FLAGS + [str(ll)],
                       capture_output=True, text=True)
    log.write_text(r.stdout + r.stderr)
    return ll.name


def main():
    files = sorted(Path('.').glob('*.ll'))
    if not files:
        sys.exit('no .ll files found in current directory')

    print(f'running alive-tv on {len(files)} files...')
    with ThreadPoolExecutor() as pool:
        futures = {pool.submit(run_one, f): f for f in files}
        for fut in as_completed(futures):
            try:
                print(f'done: {fut.result()}')
            except Exception as e:
                print(f'error: {futures[fut]}: {e}', file=sys.stderr)


if __name__ == '__main__':
    main()
