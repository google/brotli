include buildfiles/gmake/config.make

TARGETS=all clean brotli_common brotli_dec brotli_enc brotli bro help

.PHONY: $(TARGETS) install

$(TARGETS):
	@${MAKE} -C buildfiles/gmake $@

install:
	@echo "copy include and libraries to $(prefix)"
	$(error Installation is not implemented yet)
