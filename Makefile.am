## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I M4

DISTCHECK_CONFIGURE_FLAGS = --enable-werror

if BUILD_OCTAVE_MOD
octave_dir = Octave
endif

SUBDIRS = M4 man doc Win32 src $(octave_dir) examples regtest tests programs
DIST_SUBDIRS = M4 man doc Win32 src Octave examples regtest tests programs

EXTRA_DIST = libsndfile.spec.in sndfile.pc.in Scripts/android-configure.sh \
	Scripts/linux-to-win-cross-configure.sh Scripts/build-test-tarball.mk.in \
    CMakeLists.txt CMake/autogen.cmake CMake/build.cmake CMake/check.cmake \
	CMake/compiler_is_gcc.c CMake/external_libs.cmake CMake/file.cmake \
	CMake/have_decl_s_irgrp.c CMake/libsndfile.cmake


CLEANFILES = *~

pkgconfig_DATA = sndfile.pc

m4datadir = $(datadir)/aclocal

#===============================================================================

test: check-recursive

# Target to make autogenerated files.
genfiles :
	(cd src ; make genfiles)
	(cd tests ; make genfiles)

checkprograms :
	(cd src ; make libsndfile.la checkprograms)
	(cd tests ; make checkprograms)

testprogs :
	(cd src ; make testprogs)
	(cd tests ; make testprogs)


test-tarball : Scripts/build-test-tarball.mk
	(cd src ; make all libsndfile.la checkprograms)
	(cd tests ; make all checkprograms)
	make -f Scripts/build-test-tarball.mk
