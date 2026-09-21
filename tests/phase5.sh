# /usr/test/phase5 -- PLAN.md phase 5 tests, run on the Lisa by
# tests/lisaem-mmu.sh: fork/exec timing, then the Minix system call tests.
cd /usr/test
./forkbench 20
for t in test0 test1 test2 test3 test4 test5 test6 test7 test8 test9 test10 test11 test12 test13 test14 test15 test16 test17 test18 test19 test20 test21
do
	echo "phase5: $t"
	./$t
done
echo "phase5: all tests run"
