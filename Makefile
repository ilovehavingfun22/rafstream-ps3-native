# Makefile PSL1GHT untuk RAFSTREAM PS3 Native Standalone .PKG
TITLE       := RAFSTREAM PS3
APPID       := RAFSTRM01
CONTENTID   := UP0001-$(APPID)_00-RAFSTREAMPS30001

TARGET      := rafstream_ps3
BUILD       := build
SOURCES     := source
INCLUDES    := include

CFLAGS      := -O2 -Wall -I$(CURDIR)/$(INCLUDES) -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include
LDFLAGS     := -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib
LIBS        := -ltiny3d -ljpgdec -lpngdec -lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
               -lavformat -lavcodec -lswscale -lswresample -lavutil -lz \
               -laudio -lnet -lio -lsysutil -lsysmodule -lm

include $(PSL1GHT)/ppu_rules
