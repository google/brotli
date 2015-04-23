#brotli/tests

include ../shared.mk

BROTLI = ..

all: test

test: deps
	./compatibility_test.sh
	./roundtrip_test.sh

deps :
	$(MAKE) -C $(BROTLI)/tools

clean :
	rm -f testdata/*.{bro,unbro,uncompressed}
	rm -f $(BROTLI)/{enc,dec,tools}/*.{un,}bro
	$(MAKE) -C $(BROTLI)/tools clean
