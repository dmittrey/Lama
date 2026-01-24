#!/bin/bash

set -e

cd "$(dirname "$0")/../.."

now_ms() {
  perl -MTime::HiRes=time -e 'printf("%d\n", int(time()*1000))'
}

run_timed() {
  local label="$1"
  shift
  echo "$label"
  local start
  start="$(now_ms)"
  "$@"
  local end
  end="$(now_ms)"
  local elapsed=$((end - start))
  echo "$label finished in ${elapsed}ms"
}

run_timed "Running standard dune regression tests..." \
  dune test regression_long

echo "Applying dune patch for regression_long..."
git apply vm/regression/regression_long_dune.patch

echo "Patching regression_long/**/*.t files..."
find regression_long -name "*.t" -type f -exec perl -pi -e 's/^(\s*)\$\s+\.\.\/\.\.\/src\/Driver\.exe\s+-runtime\s+\.\.\/\.\.\/runtime\s+-I\s+\.\.\/\.\.\/runtime\s+-I\s+\.\.\/\.\.\/stdlib\/x64\s+-i\s+(\S+)\.lama\s+<\s+\2\.input\s*$/$1\$ ..\/..\/src\/Driver.exe -runtime ..\/..\/runtime -I ..\/..\/stdlib\/x64 -b $2.lama && ..\/..\/vm\/vm $2.bc < $2.input\n/;' {} \;

run_timed "Running regression tests on vm..." \
  dune test regression_long

git restore regression_long

echo "Done! regression_long is now configured to use vm."
