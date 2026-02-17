  $ echo "=== Testing: vmver — bytecode from file (.bc) with timing ==="
  === Testing: vmver — bytecode from file (.bc) with timing ===
  $ ../src/Driver.exe -runtime ../runtime -I ../stdlib/x64 -b ../performance/Sort.lama
  $ LAMA_TIMING=1 ./vmver Sort.bc < ../performance/Sort.input 2>perf_stderr.txt; echo "exit: $?"
  exit: 0
  $ grep -q 'verification:.*ms' perf_stderr.txt && grep -q 'execution:.*ms' perf_stderr.txt && echo "verification and execution timing reported"
  verification and execution timing reported
  $ cat perf_stderr.txt
  verification: 0.000 ms
  execution: 0.000 ms
  # +  verification: 0.160 ms
  # +  execution: 1471069.985 ms

  # bytecode interpretation (-s) - finished in 295270.730 ms
  # source-level interpreter (-i), AST interpretation - finished in 753908.150 ms
  # custom VM — bytecode from file (.bc) - finished in 1727814.300 ms
