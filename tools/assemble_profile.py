#!/usr/bin/env python3
"""Assemble a temporary, legacy-compatible profile view from canonical shared files and overrides."""
from __future__ import annotations
import argparse
import shutil
from pathlib import Path


def overlay(src: Path, dst: Path) -> None:
    if not src.exists():
        return
    for item in src.iterdir():
        target = dst / item.name
        if item.is_dir() and not item.is_symlink():
            target.mkdir(parents=True, exist_ok=True)
            overlay(item, target)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.is_dir() and not target.is_symlink():
                shutil.rmtree(target)
            shutil.copy2(item, target, follow_symlinks=False)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--profile', choices=('egor', 'stepan'), required=True)
    ap.add_argument('--dest', type=Path, required=True)
    args = ap.parse_args()
    root = args.root.resolve()
    dest = args.dest.resolve()
    profile = root / 'profiles' / args.profile
    if not (root / 'shared/plugin').is_dir() or not (root / 'multiplayer').is_dir():
        ap.error('source tree incomplete: shared/plugin or multiplayer missing')
    if not profile.is_dir():
        ap.error(f'profile not found: {args.profile}')
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    plugin = dest / 'plugin'
    multiplayer = dest / 'multiplayer'
    shutil.copytree(root / 'shared/plugin', plugin, symlinks=True)
    shutil.copytree(root / 'multiplayer', multiplayer, symlinks=True)
    overlay(profile / 'plugin-overrides', plugin)
    overlay(profile / 'multiplayer-overrides', multiplayer)
    # Profile-specific tests expect the historic ./fork path and use tests/ as their root.
    (dest / 'fork').symlink_to(plugin, target_is_directory=True)
    (dest / 'stepan').symlink_to(plugin, target_is_directory=True)
    if (profile / 'tests').is_dir():
        shutil.copytree(profile / 'tests', dest / 'tests', symlinks=True)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
