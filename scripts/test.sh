#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
for source in tests/test-theme.c tests/test-menu-layout.c tests/test-advanced-control.c tests/test-radio-tools.c tests/test-charge.c tests/test-power-role.c tests/test-web-policy.c tests/test-standby-policy.c tests/test-relay.c tests/test-network-diagnostics.c;do
 name=$(basename "$source" .c)
 cc -O1 -I panel -I panel/vendor "$source" panel/vendor/cJSON.c -lm -o "build/tests/$name"
 "build/tests/$name"
done
for source in tests/test-*.py tests/control-test.py;do python3 "$source";done
python3 tests/test-esim-computer.py
python3 tests/test-network-control.py
for source in tests/test-web-*.js;do node "$source";done
for source in web/factory/u60-enhanced.js web/factory/u60-web-advanced.js web/factory/u60-web-model.js web/factory/u60-web-esim.js;do node --check "$source";done
(cd web/controller; go test ./...)
for source in panel/*.sh scripts/build.sh scripts/test.sh scripts/portable/*.sh;do sh -n "$source";done
cargo test --locked --manifest-path esim-agent/Cargo.toml
echo 'PASS: host regression suite (not a substitute for new-device physical tests)'
