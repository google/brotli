#!python3
"""Program to dump contents of brotli files showing the compression format.
"""
import struct
from operator import itemgetter
from itertools import accumulate

class BitStream:
    """Represent a bytes object. Can read bits and prefix codes the way
    Brotli does.
    """
    def __init__(self, b):
        self.data = b
        #position in bits: byte pos is pos>>3, bit pos is pos&7
        self.pos = 0

    def __repr__(self):
        """Representation
        >>> olleke
        BitStream(pos=0:0)
        """
        return "BitStream(pos={:x}:{})".format(self.pos>>3, self.pos&7)

    def read(self, n):
        """Read n bits from the stream and return as an integer.
        Produces zero bits beyond the stream.
        >>> olleke.data[0]==27
        True
        >>> olleke.read(5)
        27

        >>> olleke
        BitStream(pos=0:5)
        """
        relevantBytes = self.data[self.pos>>3:self.pos+n+7>>3]
        value = int.from_bytes(relevantBytes, 'little')
        mask = (1<<n)-1
        value = value>>(self.pos&7) & mask
        self.pos += n
        if self.pos>len(self.data)*8:
            raise ValueError('Read past end of stream')
        return value

    def peek(self, n):
        """Peek an n bit integer from the stream without updating the pointer.
        Is is not an error to read beyond the end of the stream.
        n is at most 15
        >>> olleke.data[:2]==b'\x1b\x2e' and 0x2e1b==11803
        True
        >>> olleke.peek(15)
        11803
        """
        #with 8 we have at least one bit, so with 22 we have 15
        return int.from_bytes(
            self.data[self.pos>>3:self.pos+22>>3],
            'little')>>(self.pos&7)&(1<<n)-1

    def readPrefix(self, prefix):
        """Read a prefix value from the stream.
        prefix must be a dict from integers to objects.
        Returns the the length of the prefix value and the matching object.
        Figures out the length of the prefixes automatically.
        Gives an error if the prefix code cannot be decoded.
        >>> x = BitStream(bytes([0xdc,0xec]))
        >>> p = {0b00:0, 0b0111:1, 0b011:2, 0b10:3, 0b01:4, 0b1111:5}
        >>> x.readPrefix(p)
        (2, 0)
        >>> x.readPrefix(p)
        (4, 1)
        >>> x.readPrefix(p)
        (3, 2)
        >>> x.readPrefix(p)
        (2, 3)
        >>> x.readPrefix(p)
        (2, 4)
        >>> x.readPrefix(p)
        Traceback (most recent call last):
            ...
        ValueError: Read past end of stream
        """
        peek = self.peek(15)
        for datalen in range(1,16):
            mask = (1<<datalen)-1
            maskedPeek = peek&mask
            if sum(x&mask==maskedPeek for x in prefix)==1:
                self.pos += datalen
                if self.pos>len(self.data)*8:
                    raise ValueError('Read past end of stream')
                return datalen, prefix[maskedPeek]
        raise ValueError("Invalid prefix code")

    def verboseRead(self, n, prependComma=False, compress=False):
        """Read n bits, return binary data as string, with commas
        separating the bytes for clarity.
        prependComma prepends a comma on the left if at a byte boundary
        >>> olleke.pos = 5
        >>> olleke.verboseRead(12)
        '0,00101110,000'
        """
        result = []
        while n:
            availableBits = 8-(self.pos&7)
            bitsToRead = min(n, availableBits)
            data = self.read(bitsToRead)
            if bitsToRead==8 and compress:
                result.append('{:0{}X}h'.format(data, bitsToRead//4))
            else:
                result.append('{:0{}b}'.format(data, bitsToRead))
            n -= bitsToRead
        if self.pos&7==0 and prependComma: result[-1] = ','+result[-1]
        #since we read lsb first, the data is constructed backwards
        return ','.join(reversed(result))

    def readBytes(self, n):
        """Read n bytes from the stream.
        """
        if self.pos&7: raise ValueError('readBytes: need byte boundary')
        result = self.data[self.pos>>3:(self.pos>>3)+n]
        self.pos += 8*n
        return result

#Alphabets
class Alphabet:
    """An alphabet is one of the alphabets for different codes used.
    Normally, these are encoded using a prefix code.
    An alphabet has a size and an order,
    and every symbol has a symbol, meaning and representation.
    Some symbols require more bits at decode time;
    call extraBits to see how many.
    """
    def __len__(self):
        """Number of symbols.
        """
        raise NotImplementedError

    def __iter__(self):
        """Iterate in sort order.
        """
        return iter(range(len(self)))

    def extraBits(self, symbol):
        """Indicate how many extra bits are needed to interpret symbol
        """
        return 0

    def meaning(self, symbol, extra):
        """Give meaning of symbol together with the extra bits.
        """
        if extra: raise ValueError('Meaning with extra bits not defined')
        return str(symbol)

    def __getitem__(self, symbol):
        """Give mnemonic representation of meaning.
        """
        return str(symbol)

class LengthAlphabet:
    """The alphabet to encode symbol lengths for complex codes"""
    def __len__(self):
        return 18

    def extraBits(self, symbol):
        if symbol==16: return 2
        elif symbol==17: return 3
        else: return 0

    def meaning(self, symbol, repeat):
        """Mnemonic for symbols.
        Note that this routine is called with an precomputed repeat value.
        >>> lengthAlphabet.meaning(4, 0)
        '4'
        >>> lengthAlphabet.meaning(17, 13)
        'skip 13'
        """
        if symbol==0: return 'skip'
        elif symbol==16: return 'rep '+str(repeat)
        elif symbol==17: return 'skip '+str(repeat)
        else: return str(symbol)

    def __getitem__(self, symbol):
        if symbol==0: return '--'
        elif symbol==16: return 'Rxx'
        elif symbol==17: return 'Zxxx'
        else: return str(symbol)

lengthAlphabet = LengthAlphabet()
class BlockTypeAlphabet(Alphabet):
    def __init__(self, NBLTYPES):
        self.NBLTYPES = NBLTYPES
    def __len__(self):
        return self.NBLTYPES+2
    def __getitem__(self, symbol):
        if symbol==0: return 'prev'
        elif symbol==1: return 'inc'
        else: return str(symbol-2)

class BlockCountAlphabet(Alphabet):
    extra = [2,2,2,2,3,3,3,3,4, 4,4,4,5,5,5,5,6,6, 7,8,9,10,11,12,13,24]
    #compute start of ranges
    base = list(accumulate(1<<x for x in [0]+extra[:-1]))
    def __len__(self):
        return 26

    def extraBits(self, symbol):
        return self.extra[symbol]

    def meaning(self, symbol, extra):
        return 'L'+str(self.base[symbol]+extra)

    def __getitem__(self, symbol):
        return 'L{}+{}'.format(
            'x'*self.extra[symbol],
            self.base[symbol])

blockCountAlphabet = BlockCountAlphabet()
class DistanceAlphabet(Alphabet):
    """Represent the distance encoding.
    Ignoring offsets for the moment, the "long" encoding works as follows:
    Write the distance in binary as follows:
    1xy..yz..z, then the distance symbol consists of n..nxz..z
    Where:
    n is one less than number of bits in y
    x is a single bit
    y..y are n+1 extra bits (encoded in the bit stream)
    z..z is NPOSTFIX bits that are part of the symbol
    The offsets are so as to start at the lowest useable value:
    if 1xyyyyz = distance +(4<<POSTFIX)-NDIRECT-1
    then n..nxz..z is symbol -NDIRECT-16
    """
    def __init__(self, NPOSTFIX, NDIRECT):
        """Create a distance alphabet from the parameters.
        """
        self.NPOSTFIX = NPOSTFIX
        self.NDIRECT = NDIRECT

    def __len__(self):
        """Number of symbols.
        >>> d = DistanceAlphabet(2, 10)
        >>> len(d)
        218
        """
        return self.NDIRECT+16+(48<<self.NPOSTFIX)

    def extraBits(self, symbol):
        """Indicate how many extra bits are needed to interpret symbol
        >>> d = DistanceAlphabet(2, 10)
        >>> [d.extraBits(i) for i in range(26)]
        [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        >>> [d.extraBits(i) for i in range(26,36)]
        [1, 1, 1, 1, 1, 1, 1, 1, 2, 2]
        """
        if symbol<16+self.NDIRECT: return 0
        return 1 + ((symbol - self.NDIRECT - 16) >> (self.NPOSTFIX + 1))

    def meaning(self, dcode, dextra):
        """Explain meaning of symbol together with the extra bits.
        >>> d = DistanceAlphabet(2, 10)
        >>> d.meaning(34, 2)
        '35'
        """
        if dcode<16:
            raise NotImplementedError('Last distance buffer')
        if dcode<16+self.NDIRECT:
            return str(dcode-16)
        #we use the original formulas
        POSTFIX_MASK = (1 << self.NPOSTFIX) - 1
        ndistbits = 1 + ((dcode - self.NDIRECT - 16) >> (self.NPOSTFIX + 1))
        hcode = (dcode - self.NDIRECT - 16) >> self.NPOSTFIX
        lcode = (dcode - self.NDIRECT - 16) & POSTFIX_MASK
        offset = ((2 + (hcode & 1)) << ndistbits) - 4
        distance = ((offset + dextra) << self.NPOSTFIX) + lcode + self.NDIRECT + 1
        return str(distance)

    def meaningTuple(self, symbol, extra):
        """Give meaning of symbol as (action, value) pair
        for complex alphabets.
        """
        raise NotImplementedError

    def __getitem__(self, symbol):
        """Give mnemonic representation of meaning.
        >>> d = DistanceAlphabet(2, 10)
        >>> d[4]
        'last-1'
        >>> d[17]
        '1'
        >>> [d[x] for x in range(26, 32)]
        ['10x00-5', '10x01-5', '10x10-5', '10x11-5', '11x00-5', '11x01-5']
        >>> d[34]
        '10xx00-5'
        """
        if symbol<16:
            return ['last', '2last', '3last', '4last',
                'last-1', 'last+1', 'last-2', 'last+2', 'last-3', 'last+3',
                '2last-1', '2last+1', '2last-2', '2last+2', '2last-3', '2last+3'
                ][symbol]
        if symbol<16+self.NDIRECT:
            return str(symbol-16)
        #construct strings like "1xx01-15"
        symbol -= self.NDIRECT+16
        hcode = symbol >> self.NPOSTFIX
        lcode = symbol & (1<<self.NPOSTFIX)-1
        return '1{}{}{:0{}b}{:+d}'.format(
            hcode&1,
            'x'*(2+hcode>>1),
            lcode, self.NPOSTFIX,
            self.NDIRECT+1-(4<<self.NPOSTFIX))

class SymbolAlphabet(Alphabet):
    """Represent the alphabet of bytes. Assumes ASCII.
    """
    def __len__(self):
        """Number of symbols.
        """
        return 256

    def __getitem__(self, symbol):
        """Give mnemonic representation of meaning.
        """
        if 32<=symbol<128: return repr(chr(symbol))
        else: return '\\x{:02x}'.format(symbol)

symbolAlphabet = SymbolAlphabet()

class CommandAlphabet(Alphabet):
    """The alphabet of insert-and-copy lengths.
    """
    insertExtra = [0,0,0,0,0,0,1,1, 2,2,3,3,4,4,5,5, 6,7,8,9,10,12,14,24]
    copyExtra = [0,0,0,0,0,0,0,0, 1,1,2,2,3,3,4,4, 5,5,6,7,8,9,10,24]
    #compute start of ranges
    insertBase = [0]+list(accumulate(1<<x for x in insertExtra[:-1]))
    copyBase = list(accumulate(1<<x for x in [1]+copyExtra[:-1]))
    def __len__(self):
        return 11<<6

    def meaningTuple(self, symbol):
        """Give relevant values for computations:
        (insertCode, copyCode, dist0flag)
        """
        #determine insert and copy upper bits from table 
        row = [0,0,1,1,2,2,1,3,2,3,3][symbol>>6]
        col = [0,1,0,1,0,1,2,0,2,1,2][symbol>>6]
        #determine inserts and copy sub codes
        insertLengthCode = row<<3 | symbol>>3&7
        if row: insertLengthCode -= 8
        copyLengthCode = col<<3 | symbol&7
        return insertLengthCode, copyLengthCode, row==0

    def extraBits(self, symbol):
        i,c,ei,ec = self.meaningTuple(symbol)
        return ei+ec

    def meaning(self, symbol, extra):
        i,c,d0 = self.meaningTuple(symbol)
        copyExtraBits = self.copyExtra[c]
        ie, ce = extra>>copyExtraBits, extra&(1<<copyExtraBits)-1
        insert = self.insertBase[i]+ie
        copy = self.copyBase[c]+ce
        if d0: return 'I{}C{}D0'.format(insert, copy)
        else: return 'I{}C{}'.format(insert, copy)

    def __getitem__(self, symbol):
        """Make a nice mnemonic
        >>> c = CommandAlphabet()
        >>> for x in range(0,704,704//13):
        ...    print('{:10b}'.format(x), c[x])
                 0 I0C2&D=0
            110110 I6+xC8&D=0
           1101100 I5C22+xxx&D=0
          10100010 I4C4
          11011000 I3C10+x
         100001110 I14+xxC8
         101000100 I10+xxC22+xxx
         101111010 I98+xxxxxC14+xx
         110110000 I6+xC70+xxxxx
         111100110 I1090+xxxxxxxxxxC8
        1000011100 I26+xxxC326+xxxxxxxx
        1001010010 I322+xxxxxxxxC14+xx
        1010001000 I194+xxxxxxxC70+xxxxx
        1010111110 I22594+xxxxxxxxxxxxxxxxxxxxxxxxC1094+xxxxxxxxxx
        """
        i,c,d0 = self.meaningTuple(symbol)
        return 'I{}{}{}C{}{}{}{}'.format(
            self.insertBase[i],
            '+' if self.insertExtra[i] else '',
            'x'*self.insertExtra[i],
            self.copyBase[c],
            '+' if self.copyExtra[c] else '',
            'x'*self.copyExtra[c],
            '&D=0' if d0 else '')

commandAlphabet = CommandAlphabet()
#Prefix codes
class PrefixCode:
    """Represent a prefix code.
    For brevity, the code words are given as ints,
    LSB first.
    Surprisingly, this can be decoded without knowledge of symbol lengths.
    """
    def __init__(self, alphabet):
        self.alphabet = alphabet

    def decodeLengtTable(self, lengthTable):
        """When the lengths of the symbols are given,
        construct the codewords of the code
        """

    def decode(self, peek):
        """Decode a symbol from a peek value.
        return symbol and symbol length
        """

    def fromLayout(self, layout, name):
        """Read decode table from layout.stream and return self
        """
        stream = layout.stream
        HSKIP = stream.peek(2)
        alphabetSize = len(self.alphabet)
        symbolSize = (alphabetSize-1).bit_length()
        mask = (1<<symbolSize)-1
        table = []
        if HSKIP==1:
            layout.printItem(2, 'Simple', 0, 'prefix code '+name)
            NSYM = (stream.peek(2))+1
            layout.printItem(2, NSYM, 0, 'code words')
            for i in range(NSYM):
                symbol = stream.peek(15)&mask
                layout.printItem(symbolSize, self.alphabet[symbol], 0, '[symbol]')
                table.append(symbol)
            if NSYM==4:
                treeShape = ['wide', 'high'][stream.peek(1)]
                explanation = {'wide':'2222', 'high':'1233'}[treeShape]
                layout.printItem(1, treeShape, 0, 'lengths '+explanation)
                codeTable = [0,2,1,3] if treeShape=='wide' else [0,1,3,7]
            else:
                codeTable = [None, [0], [0,1], [0,1,3]][NSYM]
            self.decodeTable = {w:s for w,s in zip(codeTable, table)}
        else:
            self.complexPrefix(name, layout, HSKIP)
        return self

    def complexPrefix(self, name, layout, HSKIP):
        """Read complex code"""
        stream = layout.stream
        layout.printItem(2, 'Complex; HSKIP='+str(HSKIP),
            explanation='prefix code '+name)
        #read the lengths for the length code
        lengths = [1,2,3,4,0,5,17,6,16,7,8,9,10,11,12,13,14,15][HSKIP:]
        codeLengths = {}
        total = 0
        while total<32:
            prefix = {0b00:0, 0b0111:1, 0b011:2, 0b10:3, 0b01:4, 0b1111:5}
            bits, length = stream.readPrefix(prefix)
            stream.pos -= bits
            if length: total += 32>>length
            newSymbol = lengths[len(codeLengths)]
            codeLengths[newSymbol] = length
            if total>=32:
                break
            elif length:
                layout.printItem(bits, length, 0,
                    'length for '+lengthAlphabet[newSymbol])
                codeLengths[newSymbol] = length
            else:
                layout.printItem(bits, length, 0,
                    'unused: '+lengthAlphabet[newSymbol])
        assert total==32
        #build the length code
        lengthCode = PrefixCode(lengthAlphabet)
        lengthCode.decodeTable = PrefixCode.codeFromLengths(codeLengths)
        explanation = 'code: '+lengthCode.showCode(codeLengths, False)
        layout.printItem(bits, length, 0, explanation)
        #lengthCode is the code for the lengths; now get the symbol lengths
        symbolLengths = {}
        total = 0
        lastLength = 8
        alphabetIter = iter(self.alphabet)
        while total<32768:
            bits, length = stream.readPrefix(lengthCode.decodeTable)
            if length==0:
                #unused symbol
                stream.pos -= bits
                layout.printItem(bits, lengthAlphabet.meaning(length, 0), 0,
                    self.alphabet[next(alphabetIter)])
                continue
            if length==16:
                #scan series of 16s (repeat counts)
                #start with repeat count 2
                repeat = 2
                startSymbol = next(alphabetIter)
                endSymbol = next(alphabetIter)
                symbolLengths[startSymbol] = symbolLengths[endSymbol] = lastLength
                while length==16:
                    #read extra bits, jump back for printItem
                    extra = stream.peek(2)+3
                    stream.pos -= bits
                    #determine last symbol
                    oldRepeat = repeat
                    repeat = (repeat-2<<2)+extra
                    #read as many symbols as repeat increases
                    for i in range(repeat-oldRepeat):
                        endSymbol = next(alphabetIter)
                        symbolLengths[endSymbol] = lastLength
                    layout.printItem(
                        bits,
                        lengthAlphabet.meaning(16, repeat),
                        2,
                        'length {} for {}-{}'.format(
                            lastLength,
                            self.alphabet[startSymbol],
                            self.alphabet[endSymbol]))
                    #see if there are more to do
                    bits, length = stream.readPrefix(lengthCode.decodeTable)
                stream.pos -= bits
                #some code like below
                total += repeat*32768>>lastLength
            elif length==17:
                #scan series of 17s (groups of zero counts)
                #start with repeat count 2
                repeat = 2
                startSymbol = next(alphabetIter)
                endSymbol = next(alphabetIter)
                while length==17:
                    #read extra bits, jump back for printItem
                    extra = stream.peek(3)+3
                    stream.pos -= bits
                    #determine last symbol
                    oldRepeat = repeat
                    repeat = (repeat-2<<3)+extra
                    #read as many symbols as repeat increases
                    for i in range(repeat-oldRepeat):
                        endSymbol = next(alphabetIter)
                    layout.printItem(
                        bits,
                        lengthAlphabet.meaning(17, repeat),
                        3,
                        'skip symbols {}-{}'.format(
                            self.alphabet[startSymbol], self.alphabet[endSymbol]))
                    #see if there are more to do
                    bits, length = stream.readPrefix(lengthCode.decodeTable)
                #jump back to before next symbol
                stream.pos -= bits
            else:
                stream.pos -= bits
                symbol = next(alphabetIter)
                symbolLengths[symbol] = length
                layout.printItem(bits, length, 0,
                    'length of '+self.alphabet[symbol])
                total += 32768>>length
                lastLength = length
        self.decodeTable = PrefixCode.codeFromLengths(symbolLengths)
        print('End of table. Prefix code '+name+':')
        print(self.showCode(symbolLengths, True))

    @staticmethod
    def codeFromLengths(codeLengths):
        """Compute a code from a mapping symbols->lengths
        Produces the backwards code as integers as usual.
        >>> def printCode(l,c):
        ...   return ', '.join('{:0{}b}:{}'.format(w, l[s], s)
        ...                    for w,s in sorted(c.items(), key=itemgetter(1)))
        >>> l = {'A':2,'B':1,'C':3,'D':3}
        >>> PrefixCode.codeFromLengths(l)
        {0: 'B', 1: 'A', 3: 'C', 7: 'D'}
        >>> printCode(l, PrefixCode.codeFromLengths(l))
        '01:A, 0:B, 011:C, 111:D'
        >>> l = {'A':3,'B':3,'C':3,'D':3,'E':3,'F':2,'G':4,'H':4}
        >>> PrefixCode.codeFromLengths(l)
        {0: 'F', 1: 'C', 2: 'A', 3: 'E', 5: 'D', 6: 'B', 7: 'G', 15: 'H'}
        >>> printCode(l, PrefixCode.codeFromLengths(l))
        '010:A, 110:B, 001:C, 101:D, 011:E, 00:F, 0111:G, 1111:H'
        """
        #compute the backwards codes first; then reverse them
        #compute (backwards) first code for lengths
        nextCodes = [None]
        maxLen = max(codeLengths.values())
        code = 0;
        for bits in range(1, maxLen+1):
            code <<= 1
            nextCodes.append(code)
            code += sum(x==bits for x in codeLengths.values())
        result = {}
        for symbol in sorted(codeLengths):
            bits = codeLengths[symbol]
            if bits:
                bitpattern = '{:0{}b}'.format(nextCodes[bits], bits)
                result[int(bitpattern[::-1], 2)] = symbol
                nextCodes[bits] += 1
        return result

    def showCode(self, codeLengths, multiline=False):
        """Show the prefix code in a nice format.
        """
        sep = '\n' if multiline else ', '
        frm = '    {}: {}' if multiline else '{}:{}'
        #make table of all symbols with binary prefix code strings
        symbolStrings = [
            ('{:0{}b}'.format(w, codeLengths[s]), s)
            for w,s in self.decodeTable.items()
            ]
        #sort on decoding order
        symbolStrings.sort(key=lambda bs:bs[0][::-1])
        #format and return
        return sep.join(
            frm.format(b, self.alphabet[s])
            for b,s in symbolStrings)

    def readFromStream(self, stream):
        """Get symbol and extra bits from stream and return as quad
        bits, symbol, extraBits, extra
        """
        bits, symbol = stream.readPrefix(self.decodeTable)
        extraBits = self.alphabet.extraBits(symbol)
        extra = stream.read(extraBits)
        return bits, symbol, extraBits, extra

#old alphabets
class ContextAlphabet(Alphabet):
    def __init__(self, NTREES, RLEMAX):
        self.NTREES = NTREES
        self.RLEMAX = RLEMAX
    def __len__(self): return self.NTREES+self.RLEMAX
    def __getitem__(self, item):
        if item==0: return 'map #0'
        if item<=self.RLEMAX:
            return 'repeat {}+{}'.format(item, 'x'*item, 1<<item)
        return 'map #{}'.format(item-self.RLEMAX)
class Layout:
    """Class to layout the output.
    """
    #width of hexdata+bitdata
    width = 20
    def __init__(self, stream):
        self.stream = stream
        self.bitPtr = 0

    def printItem(self, bits, meaning,
            extrabits=0, explanation='', compress=False):
        """Print a line consisting of:
        - address (to bit level)
        - byte data (if a new byte is used)
        - bit data (preceded by extra bits, moves to the left)
        - symbol meaning
        - explanation
        the bits and extrabits are read from the stream and returned as ints
        >>> olleke.pos = 0
        >>> l = Layout(olleke)
        >>> l.printItem(2, "length", 0, "example") #doctest: +REPORT_NDIFF
        0000 1b                11 length     example
        >>> l.printItem(3, '') #doctest: +REPORT_NDIFF
                            110              
        >>> l.printItem(3, "width", 4) #doctest: +REPORT_NDIFF
        0001 2e         1110 ,000 width      
        >>> l.printItem(2, "data") #doctest: +REPORT_NDIFF
                      10          data       
        >>> l.printItem(3, "data", 0, 'because') #doctest: +REPORT_NDIFF
        0002 00              0,00 data       because
        """
        startAddress = self.stream.pos
        #determine bit data
        bitdata = self.stream.verboseRead(bits,
            prependComma=True, compress=compress)
        if extrabits:
            bitdata = self.stream.verboseRead(extrabits)+' '+bitdata
        #number of bytes to print
        hexdata = ''.join(map('{:02x} '.format,
            self.stream.data[startAddress+7>>3:self.stream.pos+7>>3]))
        #move bits to the right if new data, and prepend address
        if hexdata:
            self.bitPtr = 0
            addr = '{:04x}'.format(startAddress+7>>3)
        else:
            addr = ''
        #move output to the left
        self.bitPtr -= len(bitdata)
        filler = self.width+self.bitPtr-len(hexdata)
        if filler<0: filler = 0
        #build line
        print('{:4s} {:<{}s} {:10s} {}'.format(
            addr,
            hexdata+' '*filler+bitdata, self.width,
            str(meaning), explanation,
            ))

    def all(self):
        """Print entire brotli stream.
        """
        print('addr hex{:{}s}binary value      explanation'.format(
            '', self.width-9))
        print('Stream header'.center(60, '-'))
        self.streamHeader()
        self.ISLAST = False
        while not self.ISLAST:
            self.metablockHeader()
            if self.ISLAST:
                if self.lastEmpty(): break
            if self.metaBlockLength(): continue
            if not self.ISLAST:
                if self.uncompressed(): continue
            print('Block type descriptors'.center(60, '-'))
            for blockType in 'LID':
                self.blockType(blockType)
            self.NTREESI = self.NBLTYPESI
            self.distanceParams()
            self.readLiteralContextModes()
            print('Context maps'.center(60, '-'))
            for blockType in 'LD':
                self.contextMap(blockType)
            print('Prefix code lists'.center(60, '-'))
            for blockType in 'LID':
                self.prefixCodes(blockType)
            self.metablock()

    def readLiteralContextModes(self):
        print('Context modes'.center(60, '-'))
        self.literalContextModes = []
        for i in range(self.NBLTYPESL):
            value = ['LSB6', 'MSB6', 'UTF8', 'signed'][self.stream.peek(2)]
            self.printItem(2, value, 0, 'literal context mode '+str(i))

    def streamHeader(self):
        """Handle stream header"""
        prefix = {0b0100001:10, 0b0110001:11, 0b1000001:12, 0b1010001:13,
                  0b1100001:14, 0b1110001:15, 0b0:16, 0b0000001:17,
                  0b0011:18, 0b0101:19, 0b0111:20, 0b1001:21,
                  0b1011:22, 0b1101:23, 0b1111:24,
                  0b0010001:"INVALID",
                 }
        bits, WBITS = self.stream.readPrefix(prefix)
        self.stream.pos -= bits
        self.printItem(bits, WBITS, 0,
            "window size = (1<<{})-16 = {}".format(WBITS, (1<<WBITS)-16))
        self.WBITS = WBITS

    def metablockHeader(self):
        """Handle meta block header, set ISLAST bit
        """
        print('Meta block header'.center(60, '='))
        self.ISLAST = self.stream.peek(1)
        self.printItem(1, bool(self.ISLAST), 0, "ISLAST")

    def lastEmpty(self):
        """Read ISLASTEMPTY, return True if set
        """
        lastEmpty = self.stream.peek(1)&1
        self.printItem(1, bool(lastEmpty), 0, "LASTEMPTY")
        return lastEmpty

    def metaBlockLength(self):
        """Read MNIBBLES and meta block length;
        if empty block, skip block and return true.
        """
        MNIBBLES = [4,5,6,0][self.stream.peek(2)]
        self.printItem(2, MNIBBLES, 0, "Number of nibbles in MLEN-1")
        if MNIBBLES:
            MLEN = self.stream.read(MNIBBLES*4)+1
            self.stream.pos -= MNIBBLES*4
            self.printItem(
                MNIBBLES*4,
                hex(MLEN-1),
                explanation="MLEN = 1+0x{:x}={}".format(MLEN-1, MLEN),
                compress=True)
            self.MLEN = MLEN
        else:
            #empty block; skip and return False
            raise NotImplementedError('Empty block')
        return not MNIBBLES

    def uncompressed(self):
        """Read UNCOMPRESSED bit.
        If true, handle uncompressed data
        """
        ISUNCOMPRESSED = self.stream.peek(1)
        self.printItem(1, bool(ISUNCOMPRESSED), 0, "ISUNCOMPRESSED")
        if ISUNCOMPRESSED:
            if self.stream.pos&7:
                self.printItem(-self.stream.pos&7, 'ignored')
            print('Uncompressed data:')
            print(self.stream.readBytes(self.MLEN))
        return ISUNCOMPRESSED

    def blockTypeCode(self, name):
        """Read a code that is compressed like NBLTYPESL,
        and return the length"""
        prefix = {0b0:(0,1), 0b0001:(0,2),
                  0b0011:(1,3), 0b0101:(2,5), 0b0111:(3,9), 0b1001:(4,17),
                  0b1011:(5,33), 0b1101:(6,65), 0b1111:(129,256)
                 }
        bits, (extraBits, lowValue) = self.stream.readPrefix(prefix)
        value = lowValue + self.stream.read(extraBits)
        self.stream.pos -= bits+extraBits
        if extraBits==0: explanation = ''
        else: explanation = '. {}{:04b}={},{}'.format(
            'x'*extraBits,
            self.stream.peek(4),
            lowValue,
            lowValue+(1<<extraBits)-1,
            )
        self.printItem(bits, value, extraBits, name+explanation)
        return value

    def blockType(self, kind):
        """Read block type switch descriptor for given kind of blockType."""
        NBLTYPES = self.blockTypeCode('NBLTYPES'+kind)
        if NBLTYPES>=2:
            blockTypeCode = PrefixCode(BlockTypeAlphabet(NBLTYPES))
            blockTypeCode.fromLayout(self, 'block type '+kind)
            blockCountCode = PrefixCode(blockCountAlphabet)
            blockCountCode.fromLayout(self, 'block count '+kind)
            bits, symbol, extraBits, extra = blockCountCode.readFromStream(
                self.stream)
            self.stream.pos -= bits+extraBits
            self.printItem(
                bits,
                blockCountCode.alphabet.meaning(symbol, extra),
                extraBits,
                'block count '+kind)
        setattr(self, 'NBLTYPES'+kind, NBLTYPES)

    def distanceParams(self):
        """Read POSTFIX and NDIRECT"""
        self.NPOSTFIX = self.stream.peek(2)
        self.printItem(2, self.NPOSTFIX, explanation='POSTFIX')
        ndirect = self.stream.peek(4)
        self.NDIRECT = ndirect<<self.NPOSTFIX
        self.printItem(4, self.NDIRECT,
            explanation='NDIRECT = {}<<POSTFIX = {}'.format(
                ndirect, self.NDIRECT))

    def contextMap(self, kind):
        """Read context maps"""
        NTREES = self.blockTypeCode('NTREES'+kind)
        setattr(self, 'NTREES'+kind, NTREES)
        if NTREES<2: return
        #read CMAPkind
        if self.stream.peek(1):
            RLEMAX = (self.stream.peek(5)>>1)+1
            self.printItem(1, RLEMAX, 4, 'RLEMAX for context map')
        else:
            RLEMAX = 0
            self.printItem(1, RLEMAX, 0, 'no RLE for context map')
        alphabet = ContextAlphabet(NTREES, RLEMAX)
        code = PrefixCode(alphabet).fromLayout(self, 'CMAP'+kind)
        print('code for decoding context map:', code.decodeTable)
        entry = 0
        mapsize = {'L':64, 'D':4}[kind]
        cmap = [0]*(mapsize*getattr(self, 'NBLTYPES'+kind))
        while entry<len(cmap):
            bits, value = self.stream.readPrefix(code.decodeTable)
            if 0<value<=RLEMAX:
                rep = self.stream.peek(value)+(1<<value)
                self.stream.pos -= bits
                self.printItem(bits, str(rep)+' zeros', value,
                    '{}=1{}b; {},{}-{},{}'.format(
                        rep,
                        'x'*value,
                        entry//mapsize, entry%mapsize,
                        (entry+rep-1)//mapsize, (entry+rep-1)%mapsize,
                        ))
                entry += rep
            else:
                value = value and value-RLEMAX
                self.stream.pos -= bits
                self.printItem(bits, value, 0, 'Map entry {},{}'.format(
                    *divmod(entry, mapsize)))
                cmap[entry] = value
                entry +=1
        IMTF = bool(self.stream.peek(1))
        self.printItem(1, IMTF, 0, 'IMTF')
        if IMTF:
            self.IMTF(cmap)
        if kind=='L':
            print('Context maps for literal data:')
            for i in range(0, len(cmap), 64):
                print(*(
                    ''.join(map(str, cmap[j:j+8]))
                    for j in range(i, i+64, 8)
                    ))
        else:
            print('Context map for distances:')
            print(*(
                ''.join(map(str, cmap[i:i+4]))
                for i in range(0, len(cmap), 4)
                ))
        setattr(self, 'CMAP'+kind, cmap)

    @staticmethod
    def IMTF(v):
        """In place inverse move to front transform.
        """
        #mtf is initialized virtually with range(infinity)
        mtf = []
        for i, vi in enumerate(v):
            #get old value from mtf. If never seen, take virtual value
            try: value = mtf.pop(vi)
            except IndexError: value = vi
            #put value at front
            mtf.insert(0, value)
            #replace transformed value
            v[i] = value

    def prefixCodes(self, kind):
        """Read prefix code array"""
        prefixes = []
        numberOfTrees = getattr(self, 'NTREES'+kind)
        for i in range(numberOfTrees):
            if kind=='L': alphabet = symbolAlphabet
            elif kind=='I': alphabet = commandAlphabet
            elif kind=='D': alphabet = DistanceAlphabet(
                self.NPOSTFIX, self.NDIRECT)
            else: raise NotImplementedError('Internal error')
            prefixes.append(PrefixCode(alphabet).fromLayout(self,
                '{}{} of {}'.format(kind, i, numberOfTrees)))
        setattr(self, 'HTREE'+kind, prefixes)

    def metablock(self):
        print('Meta block contents'.center(60, '='))
"""
    You tell the class what you read, what it means, in what contexts,
    and ask it to print the results.
    We distinguish contexts in three levels:
    Stream header
    Metablock header
        Metablock parameters
        Block type descriptors
            Literal
            Sizes
            Distances
        Distance parameters
        Context maps
            Literal
            Distance
        Prefix codes
            Literal
            Sizes
            Distances
    Data
        Sizes
            Block switch (type, size)
            Prefix+extra bits
        Literals
            Block switch (type, size)
            Prefix+extra bits
        Distance
            Block switch (type, size)
            Prefix+extra bits
"""
__test__ = {
'olleke': """
    >>> olleke.pos = 0
    >>> try: Layout(olleke).all()
    ... except NotImplementedError: pass
    ... #doctest: +REPORT_NDIFF
    addr hex           binary value      explanation
    Stream header --------------------------------------------------
    0000 1b              1011 22         window size = (1<<22)-16 = 4194288
    Meta block header ==================================================
                        1     True       ISLAST
                       0      False      LASTEMPTY
                    ,00       4          Number of nibbles in MLEN-1
    0001 2e 00       ,00h,2Eh 0x2e       MLEN = 1+0x2e=47
    Block type descriptors --------------------------------------------------
    0003 00                 0 1          NBLTYPESL
                           0  1          NBLTYPESI
                          0   1          NBLTYPESD
                        00    0          POSTFIX
    0004 44             0,000 0          NDIRECT = 0<<POSTFIX = 0
                      10      UTF8       literal context mode 0
    Context maps --------------------------------------------------
                     0        1          NTREESL
                    0         1          NTREESD
    Prefix code lists --------------------------------------------------
                  10          Complex; HSKIP=2 prefix code L0 of 1
    0005 4f               1,0 3          length for 3
                      0111    1          length for 4
                    10        3          length for --
    0006 d6               0,0 0          unused: 5
                       011    2          code: 0:4, 01:Zxxx, 011:--, 111:3
    0007 95           1,11 01 skip 10    skip symbols \\x00-\\x09
                     0        4          length of \\x0a
               001 01         skip 4     skip symbols \\x0b-\\x0e
    0008 44           010 0,1 skip 21    skip symbols \\x0b-\\x1f
                     0        4          length of ' '
                    0         4          length of '!'
    0009 cb           011 ,01 skip 6     skip symbols '"'-"'"
                110 01        skip 41    skip symbols '"'-'J'
    000a 82                 0 4          length of 'K'
                      000 01  skip 3     skip symbols 'L'-'N'
                     0        4          length of 'O'
    000b 4d              01,1 skip       'P'
                      011     skip       'Q'
                     0        4          length of 'R'
    000c 88           000 ,01 skip 3     skip symbols 'S'-'U'
                100 01        skip 15    skip symbols 'S'-'a'
    000d b6                 0 4          length of 'b'
                         011  skip       'c'
                      011     skip       'd'
    000e 27              11,1 3          length of 'e'
                   010 01     skip 5     skip symbols 'f'-'j'
                 ,0           4          length of 'k'
    000f 1f               111 3          length of 'l'
                       011    skip       'm'
                      0       4          length of 'n'
                    ,0        4          length of 'o'
    0010 c1            000 01 skip 3     skip symbols 'p'-'r'
                      0       4          length of 's'
    0011 b4              0,11 skip       't'
                        0     4          length of 'u'
                      01      Simple     prefix code I0 of 1
                    11        4          code words
    End of table. Code:
        000: 'e'
        0001: 'O'
        0010: \x0a
        0011: 'n'
        100: 'l'
        0101: 'b'
        0110: '!'
        0111: 's'
        1001: 'R'
        1010: ' '
        1011: 'o'
        1101: 'k'
        1110: 'K'
        1111: 'u'
    0012 2a      ,00101010,10 I5C4       [symbol]
    0013 b5 ec    00,10110101 I6+xC7     [symbol]
    0015 22       0010,111011 I8+xC5     [symbol]
    0016 8c       001100,0010 I0C14+xx   [symbol]
                 0            wide       lengths 2222
    0017 74               0,1 Simple     prefix code D0 of 1
                        10    3          code words
    0018 a6           0,01110 2last-3    [symbol]
                010011        11xx0-3    [symbol]
    0019 aa           01010,1 11xxx0-3   [symbol]
    Meta block contents ==================================================
    """,

'file': """
    >>> Layout(BitStream(
    ... open("H:/Downloads/brotli-master/tests/testdata/10x10y.compressed",'rb')
    ...     .read())).all()
    addr hex           binary value      explanation
    Stream header --------------------------------------------------
    0000 1b              1011 22         window size = (1<<22)-16 = 4194288
    Meta block header ==================================================
                        1     True       ISLAST
                       0      False      LASTEMPTY
                    ,00       4          Number of nibbles in MLEN-1
    0001 13 00       ,00h,13h 0x13       MLEN = 1+0x13=20
    Block type descriptors --------------------------------------------------
    0003 00                 0 1          NBLTYPESL
                           0  1          NBLTYPESI
                          0   1          NBLTYPESD
                        00    0          POSTFIX
    0004 a4             0,000 0          NDIRECT = 0<<POSTFIX = 0
                      10      UTF8       literal context mode 0
    Context maps --------------------------------------------------
                     0        1          NTREESL
                    0         1          NTREESD
    Prefix code lists --------------------------------------------------
                  01          Simple     prefix code L0 of 1
    0005 b0               0,1 2          code words
    0006 b2         0,1011000 'X'        [symbol]
    0007 ea         0,1011001 'Y'        [symbol]
                  01          Simple     prefix code I0 of 1
                01            2          code words
    0008 81       0000001,111 I1C9&D=0   [symbol]
    0009 47 02   0,01000111,1 I1C9       [symbol]
               01             Simple     prefix code D0 of 1
             00               1          code words
    000b 8a           010,000 10x0-3     [symbol]
    Meta block contents ==================================================
    """,

'XY': """
    >>> Layout(BitStream(brotli.compress('X'*10+'Y'*10))).all()
    addr hex           binary value      explanation
    Stream header --------------------------------------------------
    0000 1b              1011 22         window size = (1<<22)-16 = 4194288
    Meta block header ==================================================
                        1     True       ISLAST
                       0      False      LASTEMPTY
                    ,00       4          Number of nibbles in MLEN-1
    0001 13 00       ,00h,13h 0x13       MLEN = 1+0x13=20
    Block type descriptors --------------------------------------------------
    0003 00                 0 1          NBLTYPESL
                           0  1          NBLTYPESI
                          0   1          NBLTYPESD
                        00    0          POSTFIX
    0004 a4             0,000 0          NDIRECT = 0<<POSTFIX = 0
                      10      UTF8       literal context mode 0
    Context maps --------------------------------------------------
                     0        1          NTREESL
                    0         1          NTREESD
    Prefix code lists --------------------------------------------------
                  01          Simple     prefix code L0 of 1
    0005 b0               0,1 2          code words
    0006 b2         0,1011000 'X'        [symbol]
    0007 82         0,1011001 'Y'        [symbol]
                  01          Simple     prefix code I0 of 1
                00            1          code words
    0008 84       0000100,100 I4C6&D=0   [symbol]
    0009 00               0,1 Simple     prefix code D0 of 1
                        00    1          code words
    000a e0           0,00000 last       [symbol]
    Meta block contents ==================================================
    """,
}

if __name__=='__main__':
    import sys
    if len(sys.argv)>1:
        Layout(BitStream(open(sys.argv[1],'rb').read())).all()
    else:
        sys.path.append("h:/Persoonlijk/bin")
        import brotli
        olleke = BitStream(brotli.compress(
            'Olleke bolleke\nRebusolleke\nOlleke bolleke\nKnol!'))
        import doctest
        doctest.testmod(optionflags=doctest.REPORT_NDIFF)
