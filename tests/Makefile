#brotli/tests

include ../shared.mk

BROTLI = ..

all: test

test: deps
	./compatibility_test.sh
	./roundtrip_test.sh

deps :
	make -C $(BROTLI)/tools

clean :
	rm -f testdata/*.{bro,unbro,uncompressed}
	rm -f $(BROTLI)/{enc,dec,tools}/*.{un,}bro
	make -C $(BROTLI)/tools clean
