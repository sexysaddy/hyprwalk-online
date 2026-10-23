#!/usr/bin/env bash
# Unified source/core tests. Full plugin runtime is intentionally a separate target-system gate.
set -Eeuo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)"
JOBS="${HYPRWALK_JOBS:-$(nproc 2>/dev/null || echo 2)}"; SKIP_CORE=0; SKIP_GRAPHICS=0; FULL_CORE=0
while (($#)); do case "$1" in
 --jobs) [[ $# -ge 2 ]] || { echo '--jobs requires a value' >&2; exit 2; }; JOBS="$2"; shift 2;;
 --skip-core) SKIP_CORE=1; shift;; --skip-graphics) SKIP_GRAPHICS=1; shift;; --full-core-tests) FULL_CORE=1; shift;;
 -h|--help) echo 'Usage: ./test.sh [--jobs N] [--skip-core] [--skip-graphics] [--full-core-tests]'; exit 0;;
 *) echo "Unknown option: $1" >&2; exit 2;; esac; done
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'jobs must be a positive integer' >&2; exit 2; }

printf '== Shell syntax ==\n'
while IFS= read -r -d '' file; do bash -n "$file"; done < <(find "$ROOT/shared" "$ROOT/multiplayer" "$ROOT/profiles" "$ROOT/tools" "$ROOT/tests" -type f -name '*.sh' -print0)
bash -n "$ROOT/build.sh"; bash -n "$ROOT/test.sh"; echo 'PASS: shell syntax'

printf '== Python syntax ==\n'
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1]); count=0
for base in (root/'tools',root/'tests',root/'multiplayer',root/'profiles'):
    if not base.exists(): continue
    for path in base.rglob('*.py'):
        if any(part in {'__pycache__','build','.git'} for part in path.parts): continue
        compile(path.read_text(encoding='utf-8'),str(path),'exec'); count+=1
print(f'PASS: syntax-compiled {count} Python files without writing bytecode')
PY

printf '== Package hygiene ==\n'
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys
r=Path(sys.argv[1]); bad=[]
for p in r.rglob('*'):
    if not p.is_file(): continue
    rel=p.relative_to(r).as_posix()
    if p.suffix in {'.o','.d','.so','.a','.pyc'} or '__pycache__' in p.parts: bad.append(rel)
    if rel.endswith('/assets/viewmodels/cs16/v_knife.mdl'): bad.append(rel+' (redistribution permission not confirmed)')
if bad:
    print('FAIL: package hygiene problems:\n'+'\n'.join(bad)); raise SystemExit(1)
print('PASS: no build artifacts or unapproved v_knife.mdl in source package')
PY

printf '== Profile assembly ==\n'; python3 "$ROOT/tests/test_profile_assembly.py"
printf '== Build mode contracts ==\n'; python3 "$ROOT/tests/test_build_modes.py"
printf '== Video error isolation contract ==\n'; python3 "$ROOT/tests/test_video_error_isolation.py"
printf '== SDP payload negotiation regression contract ==\n'; python3 "$ROOT/tests/test_sdp_negotiation_contract.py"
printf '== WebRTC incoming-pad routing regression contract ==\n'; python3 "$ROOT/tests/test_webrtc_pad_routing_contract.py"
printf '== Screen capture contract ==\n'; python3 "$ROOT/tests/test_stream_capture_contract.py"
printf '== WebRTC build/preflight contract (mocked; not live WebRTC) ==\n'; bash "$ROOT/tests/test_webrtc_build_contract.sh"

if ((SKIP_GRAPHICS==0)); then
printf '== Egor graphics/viewmodel tests ==\n'
TMP="$(mktemp -d "${TMPDIR:-/tmp}/hyprwalk-unified-test.XXXXXX")"; trap 'rm -rf -- "$TMP"' EXIT INT TERM
python3 "$ROOT/tools/assemble_profile.py" --root "$ROOT" --profile egor --dest "$TMP/egor"
bash "$TMP/egor/tests/test-graphics.sh"
bash "$TMP/egor/tests/test-viewmodel.sh"
fi

if ((SKIP_CORE==0)); then
  for profile in egor stepan; do
    echo "== Clean standalone multiplayer build/tests: $profile =="
    args=(--profile "$profile" --mode core --jobs "$JOBS")
    if ((FULL_CORE)); then args+=(--full-core-tests); fi
    "$ROOT/build.sh" "${args[@]}"
  done
fi
printf 'PASS: root test suite completed. Note: it does not load the plugin into Hyprland or validate live WebRTC.\n'
