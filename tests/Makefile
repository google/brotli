#brotli/tests

BROTLI = ..

all: test

test: deps
	./compatibility_test.sh
	./roundtrip_test.sh

deps :
	$(MAKE) -C $(BROTLI) brotli

clean :
	rm -f testdata/*.{bro,unbro,uncompressed}
	rm -f $(BROTLI)/{enc,dec,tools}/*.{un,}brotli
	$(MAKE) -C $(BROTLI)/tools clean
