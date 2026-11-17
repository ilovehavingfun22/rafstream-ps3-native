TITLE       := RAFSTREAM PS3
APPID       := RAFSTRM01
CONTENTID   := UP0001-$(APPID)_00-RAFSTREAMPS30001

TARGET      := rafstream_ps3
BUILD       := build
SOURCES     := source
INCLUDES    := include

CFLAGS      := -O2 -Wall -I$(CURDIR)/$(INCLUDES) -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include
LDFLAGS     := -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib
EXTRA_LIBS  ?= -lmbedtls -lmbedx509 -lmbedcrypto -lavformat -lavcodec -lswscale -lswresample -lavutil
LIBS        := -ltiny3d -lrsx -lgcm_sys -ljpgdec -lpngdec -lcurl $(EXTRA_LIBS) -lz \
               -laudio -lnet -lio -lsysutil -lsysmodule -lm

include $(PSL1GHT)/ppu_rules

PKG_DIR     := pkg_build

pkg: $(TARGET).self
	@rm -rf $(PKG_DIR)
	@mkdir -p $(PKG_DIR)/USRDIR
	@cp ICON0.PNG $(PKG_DIR)/ICON0.PNG
	@[ -f PIC1.PNG ] && cp PIC1.PNG $(PKG_DIR)/PIC1.PNG || true
	@$(FSELF) -n $(TARGET).elf $(PKG_DIR)/USRDIR/EBOOT.BIN || make_fself $(TARGET).elf $(PKG_DIR)/USRDIR/EBOOT.BIN
	@sfo.py --title "$(TITLE)" --appid "$(APPID)" -f sfo.xml $(PKG_DIR)/PARAM.SFO || $(PSL1GHT)/bin/sfo.py --title "$(TITLE)" --appid "$(APPID)" -f sfo.xml $(PKG_DIR)/PARAM.SFO
	@pkg.py --contentid $(CONTENTID) $(PKG_DIR)/ RAFSTREAM_PS3.pkg || $(PSL1GHT)/bin/pkg.py --contentid $(CONTENTID) $(PKG_DIR)/ RAFSTREAM_PS3.pkg
	@ls -lh RAFSTREAM_PS3.pkg
