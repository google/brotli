#brotli/tests

BROTLI = ..

all: test

test: deps
	./compatibility_test.sh
	./roundtrip_test.sh

deps :
	$(MAKE) -C $(BROTLI) bro

clean :
	rm -f testdata/*.{bro,unbro,uncompressed}
	rm -f $(BROTLI)/{enc,dec,tools}/*.{un,}bro
	$(MAKE) -C $(BROTLI)/tools clean
