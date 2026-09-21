#!/bin/bash
# Build m68k-minix-gcc: gcc 16.2.0 for m68k-elf with size_t and ptrdiff_t
# following int under -mshort (gcc-16.2.0-mshort-size_t.patch), and a
# libgcc built for the 68000 with -mshort. See docs/toolchain.md.
#
# Needs Homebrew m68k-elf-binutils, gmp, mpfr, libmpc, isl and zstd.
#
# usage: toolchain/build-gcc.sh [PREFIX]   (default ~/opt/m68k-minix)

set -euo pipefail

GCC_VERSION=16.2.0
GCC_SHA256=e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e
GCC_URL=https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz

HERE=$(cd "$(dirname "$0")" && pwd)
PREFIX=${1:-$HOME/opt/m68k-minix}
WORK=${WORK:-$HOME/src/m68k-minix-gcc}
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
BREW=$(brew --prefix)

mkdir -p "$WORK"
cd "$WORK"

if [ ! -f gcc-$GCC_VERSION.tar.xz ]; then
	curl -fSL --connect-timeout 20 -o gcc-$GCC_VERSION.tar.xz "$GCC_URL"
fi
echo "$GCC_SHA256  gcc-$GCC_VERSION.tar.xz" | shasum -a 256 -c

rm -rf gcc-$GCC_VERSION build
tar xf gcc-$GCC_VERSION.tar.xz
(cd gcc-$GCC_VERSION && patch -p1 < "$HERE/gcc-$GCC_VERSION-mshort-size_t.patch")

mkdir build
cd build
../gcc-$GCC_VERSION/configure \
	--target=m68k-elf \
	--prefix="$PREFIX" \
	--program-prefix=m68k-minix- \
	--with-cpu=68000 \
	--disable-multilib \
	--enable-languages=c \
	--without-headers \
	--with-newlib \
	--disable-shared \
	--disable-threads \
	--disable-libssp \
	--disable-libquadmath \
	--disable-libgomp \
	--disable-nls \
	--with-as="$BREW/bin/m68k-elf-as" \
	--with-ld="$BREW/bin/m68k-elf-ld" \
	--with-gmp="$(brew --prefix gmp)" \
	--with-mpfr="$(brew --prefix mpfr)" \
	--with-mpc="$(brew --prefix libmpc)" \
	--with-isl="$(brew --prefix isl)" \
	--with-zstd="$(brew --prefix zstd)" \
	AR_FOR_TARGET="$BREW/bin/m68k-elf-ar" \
	RANLIB_FOR_TARGET="$BREW/bin/m68k-elf-ranlib" \
	NM_FOR_TARGET="$BREW/bin/m68k-elf-nm" \
	OBJDUMP_FOR_TARGET="$BREW/bin/m68k-elf-objdump" \
	STRIP_FOR_TARGET="$BREW/bin/m68k-elf-strip" \
	CFLAGS_FOR_TARGET="-O2 -mcpu=68000 -mshort"

make -j"$JOBS" all-gcc
make -j"$JOBS" all-target-libgcc
make install-gcc install-target-libgcc

echo "installed: $PREFIX/bin/m68k-minix-gcc"
