  $ echo "=== Testing: bcfreq util — bytecode from file (.bc) ==="
  === Testing: bcfreq util — bytecode from file (.bc) ===
  $ ../src/Driver.exe -runtime ../runtime -I ../stdlib/x64 -b performance/Sort.lama
  $ ./bcfreq Sort.bc
  OneSizeSeq counts:
  31 : drop
  28 : dup
  21 : elem
  16 : const 1
  11 : const 0
  7 : ld A(0)
  5 : end
  5 : jmp 762
  4 : sexp 0 2
  3 : st L(0)
  3 : ld L(0)
  3 : array 2
  3 : call 351 1
  3 : call Barray 2
  3 : jmp 350
  3 : ld L(3)
  2 : ld L(1)
  2 : call 43 1
  2 : begin 1 0
  2 : binop eq
  2 : call 151 1
  2 : jmp 116
  2 : tag 0 2
  1 : stop
  1 : st L(5)
  1 : st L(4)
  1 : st L(3)
  1 : st L(2)
  1 : st L(1)
  1 : line 9
  1 : line 7
  1 : line 6
  1 : line 5
  1 : line 3
  1 : line 27
  1 : line 25
  1 : line 24
  1 : line 20
  1 : line 18
  1 : line 16
  1 : line 15
  1 : line 14
  1 : ld L(5)
  1 : ld L(4)
  1 : ld L(2)
  1 : jmp 734
  1 : jmp 715
  1 : jmp 386
  1 : jmp 336
  1 : jmp 262
  1 : fail 7 17
  1 : fail 14 9
  1 : const 10000
  1 : cjmpz 600
  1 : cjmpz 274
  1 : cjmpz 191
  1 : cjmpz 106
  1 : cjmpnz 637
  1 : cjmpnz 428
  1 : cjmpnz 392
  1 : cjmpnz 280
  1 : cjmpnz 197
  1 : call 117 1
  1 : binop sub
  1 : binop gt
  1 : begin 2 0
  1 : begin 1 6
  1 : begin 1 1
  TwoSizeSeq counts:
  13 : const 1 -> elem
  11 : drop -> dup
  11 : dup -> const 1
  10 : drop -> drop
  8 : const 0 -> elem
  7 : dup -> const 0
  7 : elem -> drop
  6 : jmp 762 -> end
  4 : call 351 1 -> dup
  4 : dup -> dup
  4 : jmp 350 -> end
  3 : call 351 1 -> begin 1 6
  3 : call Barray 2 -> jmp 762
  3 : dup -> array 2
  3 : elem -> st L(0)
  3 : jmp 116 -> end
  3 : st L(0) -> drop
  2 : jmp 262 -> dup
  2 : elem -> const 1
  2 : elem -> const 0
  2 : dup -> tag 0 2
  2 : cjmpnz 197 -> drop
  2 : call 43 1 -> sexp 0 2
  2 : call 43 1 -> call 117 1
  2 : call 43 1 -> begin 1 0
  2 : call 351 1 -> const 1
  2 : call 151 1 -> jmp 350
  2 : call 151 1 -> end
  2 : call 151 1 -> begin 1 1
  2 : call 117 1 -> end
  2 : cjmpnz 280 -> drop
  2 : cjmpnz 392 -> drop
  2 : cjmpnz 428 -> drop
  2 : cjmpnz 637 -> drop
  2 : cjmpz 106 -> ld A(0)
  2 : cjmpz 191 -> dup
  2 : cjmpz 600 -> const 1
  2 : cjmpz 274 -> dup
  2 : end -> begin 1 0
  2 : jmp 734 -> dup
  2 : sexp 0 2 -> call Barray 2
  1 : tag 0 2 -> cjmpnz 428
  1 : tag 0 2 -> cjmpnz 392
  1 : st L(5) -> drop
  1 : st L(4) -> drop
  1 : st L(3) -> drop
  1 : st L(2) -> drop
  1 : st L(1) -> drop
  1 : sexp 0 2 -> jmp 116
  1 : sexp 0 2 -> call 351 1
  1 : line 9 -> ld A(0)
  1 : line 7 -> ld L(2)
  1 : line 6 -> ld L(1)
  1 : line 5 -> ld L(3)
  1 : line 3 -> ld A(0)
  1 : line 27 -> const 10000
  1 : line 25 -> line 27
  1 : line 24 -> ld A(0)
  1 : line 20 -> ld A(0)
  1 : line 18 -> line 20
  1 : line 16 -> ld L(0)
  1 : line 15 -> ld L(0)
  1 : line 14 -> ld A(0)
  1 : ld L(5) -> ld L(3)
  1 : ld L(4) -> sexp 0 2
  1 : ld L(3) -> ld L(4)
  1 : ld L(3) -> ld L(1)
  1 : ld L(3) -> ld L(0)
  1 : ld L(2) -> call 351 1
  1 : ld L(1) -> ld L(3)
  1 : ld L(1) -> binop gt
  1 : ld L(0) -> sexp 0 2
  1 : ld L(0) -> jmp 350
  1 : ld L(0) -> call 151 1
  1 : ld A(0) -> ld A(0)
  1 : ld A(0) -> dup
  1 : ld A(0) -> const 1
  1 : ld A(0) -> cjmpz 106
  1 : ld A(0) -> call Barray 2
  1 : ld A(0) -> call 351 1
  1 : ld A(0) -> call 151 1
  1 : jmp 762 -> line 7
  1 : jmp 762 -> jmp 762
  1 : jmp 762 -> fail 7 17
  1 : jmp 762 -> dup
  1 : jmp 715 -> fail 7 17
  1 : jmp 715 -> dup
  1 : jmp 386 -> dup
  1 : jmp 386 -> drop
  1 : jmp 350 -> fail 14 9
  1 : jmp 350 -> dup
  1 : jmp 336 -> fail 14 9
  1 : jmp 336 -> dup
  1 : jmp 116 -> const 0
  1 : fail 7 17 -> jmp 762
  1 : fail 14 9 -> jmp 350
  1 : end -> stop
  1 : end -> begin 1 6
  1 : end -> begin 1 1
  1 : elem -> st L(5)
  1 : elem -> st L(4)
  1 : elem -> st L(3)
  1 : elem -> st L(2)
  1 : elem -> st L(1)
  1 : elem -> sexp 0 2
  1 : elem -> dup
  1 : dup -> drop
  1 : drop -> line 5
  1 : drop -> line 16
  1 : drop -> line 15
  1 : drop -> ld L(5)
  1 : drop -> jmp 734
  1 : drop -> jmp 715
  1 : drop -> jmp 386
  1 : drop -> jmp 336
  1 : drop -> jmp 262
  1 : drop -> const 0
  1 : const 10000 -> call 43 1
  1 : const 1 -> line 6
  1 : const 1 -> binop sub
  1 : const 1 -> binop eq
  1 : const 0 -> line 9
  1 : const 0 -> jmp 116
  1 : const 0 -> binop eq
  1 : cjmpz 600 -> line 7
  1 : cjmpz 274 -> drop
  1 : cjmpz 191 -> drop
  1 : cjmpz 106 -> const 0
  1 : cjmpnz 637 -> dup
  1 : cjmpnz 428 -> dup
  1 : cjmpnz 392 -> dup
  1 : cjmpnz 280 -> dup
  1 : cjmpnz 197 -> dup
  1 : call 117 1 -> begin 1 0
  1 : binop sub -> call 43 1
  1 : binop gt -> cjmpz 600
  1 : binop eq -> cjmpz 274
  1 : binop eq -> cjmpz 191
  1 : begin 2 0 -> line 25
  1 : begin 1 6 -> line 3
  1 : begin 1 1 -> line 14
  1 : begin 1 0 -> line 24
  1 : begin 1 0 -> line 18
  1 : array 2 -> cjmpnz 637
  1 : array 2 -> cjmpnz 280
  1 : array 2 -> cjmpnz 197
