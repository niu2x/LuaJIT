##############################################################################
# LuaJIT top level Makefile for installation. Requires GNU Make.
#
# Please read doc/install.html before changing any variables!
#
# Suitable for POSIX platforms (Linux, *BSD, OSX etc.).
# Note: src/Makefile has many more configurable options.
#
# ##### This Makefile is NOT useful for Windows! #####
# For MSVC, please follow the instructions given in src/msvcbuild.bat.
# For MinGW and Cygwin, cd to src and run make with the Makefile there.
#
# Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
##############################################################################

MAJVER=  2
MINVER=  1
ABIVER=  5.1

MMVERSION= $(MAJVER).$(MINVER)

INSTALL_DEP= src/luajit

default all $(INSTALL_DEP):
	@echo "==== Building LuaJIT $(MMVERSION) ===="
	$(MAKE) -C src
	@echo "==== Successfully built LuaJIT $(MMVERSION) ===="

##############################################################################
clean:
	$(MAKE) -C src clean

.PHONY: all clean

##############################################################################
