# /usr/test/cmds -- exercise the multi-file commands without a terminal;
# run on the Lisa by tests/lisaem-cmds.sh, which checks the output.
cd /tmp
echo "cmds: make"
echo 'all: hello.out' > Makefile
echo 'hello.out: hello.in' >> Makefile
echo '	cat hello.in hello.in > hello.out' >> Makefile
echo hi > hello.in
make
cat hello.out
echo "cmds: m4"
echo 'define(NAME,Lisa)hello NAME' | m4
echo "cmds: bawk"
echo 'one two three' | bawk '{ print $2 }'
echo "cmds: nroff"
echo '.ce' > t.nr
echo 'centered text' >> t.nr
nroff t.nr
echo "cmds: patch"
echo 'line one' > a
echo 'line two' > b
diff a b > ab.diff
cp a c
patch c ab.diff
cat c
echo "cmds: ar"
ar r lib.a a b
ar t lib.a
echo "cmds: indent"
echo 'main(){int x;x=1;}' > i.c
indent i.c i2.c
cat i2.c
echo "cmds: done"
