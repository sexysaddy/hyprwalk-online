#!/usr/bin/env python3
"""Validates the generated source view byte-for-byte against the checked-in manifests."""
from pathlib import Path
import hashlib, json, subprocess, sys, tempfile
ROOT=Path(__file__).resolve().parents[1]
MANIFEST=json.loads((ROOT/'SOURCE_MANIFESTS.json').read_text(encoding='utf-8'))

def file_map(base):
    return {p.relative_to(base).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(base.rglob('*')) if p.is_file() and not p.is_symlink()}
for profile in ('egor','stepan'):
    with tempfile.TemporaryDirectory(prefix='hyprwalk-profile-test-') as td:
        dest=Path(td)/'view'
        subprocess.run([sys.executable,str(ROOT/'tools/assemble_profile.py'),'--root',str(ROOT),'--profile',profile,'--dest',str(dest)],check=True)
        assert (dest/'plugin/Makefile').is_file(), f'{profile}: plugin Makefile absent'
        assert (dest/'multiplayer/Makefile').is_file(), f'{profile}: multiplayer Makefile absent'
        assert (dest/'fork').is_symlink(), f'{profile}: compatibility fork symlink absent'
        assert (dest/'stepan').is_symlink(), f'{profile}: compatibility stepan symlink absent'
        plugin=file_map(dest/'plugin'); multiplayer=file_map(dest/'multiplayer')
        assert plugin==MANIFEST[profile]['plugin'], f'{profile}: plugin view differs from checked-in SHA-256 manifest'
        assert multiplayer==MANIFEST[profile]['multiplayer'], f'{profile}: multiplayer view differs from checked-in SHA-256 manifest'
        if profile=='egor':
            assert (dest/'plugin/src/viewmodel_model.cpp').is_file(), 'Egor viewmodel source missing'
            assert not (dest/'plugin/assets/viewmodels/cs16/v_knife.mdl').exists(), 'restricted game asset accidentally bundled'
        print(f'PASS: {profile} assembled byte-for-byte; {len(plugin)} plugin files, {len(multiplayer)} multiplayer files')
print('PASS: profile assembly SHA-256 manifests')
