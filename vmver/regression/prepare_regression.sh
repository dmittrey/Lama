#!/bin/bash

set -e

cd "$(dirname "$0")/../.."

echo "Removing tests that don't compile with standard compiler..."
rm -f regression/test054.input regression/test054.lama regression/test054.t
rm -f regression/test074.input regression/test074.lama regression/test074.t
rm -f regression/test110.input regression/test110.lama regression/test110.t
rm -f regression/test111.input regression/test111.lama regression/test111.t
rm -f regression/test803.input regression/test803.lama regression/test803.t

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

echo "Applying dune patch for regression..."
git apply vmver/regression/regression_dune.patch

echo "Patching regression/test*.t files..."
find regression -name "test*.t" -type f -exec perl -pi -e 's/^(\s*)\$\s+\.\.\/src\/Driver\.exe\s+-runtime\s+\.\.\/runtime\s+-I\s+\.\.\/stdlib\/x64\s+-i\s+(test\d+)\.lama\s+<\s+\2\.input\s*$/$1\$ ..\/src\/Driver.exe -runtime ..\/runtime -I ..\/stdlib\/x64 -b $2.lama && ..\/vmver\/vmver $2.bc < $2.input\n/;' {} \;

run_timed "Running regression tests on vm..." \
  dune test regression stdlib/regression

git restore regression

echo "Done! regression is now configured to use vm."
