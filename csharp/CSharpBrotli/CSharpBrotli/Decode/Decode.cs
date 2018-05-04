using System;

namespace CSharpBrotli.Decode
{
    public sealed class Decode
    {
        private const int DEFAULT_CODE_LENGTH = 8;
        private const int CODE_LENGTH_REPEAT_CODE = 16;
        private const int NUM_LITERAL_CODES = 256;
        private const int NUM_INSERT_AND_COPY_CODES = 704;
        private const int NUM_BLOCK_LENGTH_CODES = 26;
        private const int LITERAL_CONTEXT_BITS = 6;
        private const int DISTANCE_CONTEXT_BITS = 2;

        private const int HUFFMAN_TABLE_BITS = 8;
        private const int HUFFMAN_TABLE_MASK = 0xFF;

        private const int CODE_LENGTH_CODES = 18;
        private static readonly int[] CODE_LENGTH_CODE_ORDER =
        {
            1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        };

        private const int NUM_DISTANCE_SHORT_CODES = 16;
        private static readonly int[] DISTANCE_SHORT_CODE_INDEX_OFFSET =
        {
            3, 2, 1, 0, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2
        };

        private static readonly int[] DISTANCE_SHORT_CODE_VALUE_OFFSET =
        {
            0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3
        };

        /// <summary>
        /// Static Huffman code for the code length code lengths.
        /// </summary>
        private static readonly int[] FIXED_TABLE =
        {
            0x020000, 0x020004, 0x020003, 0x030002, 0x020000, 0x020004, 0x020003, 0x040001,
            0x020000, 0x020004, 0x020003, 0x030002, 0x020000, 0x020004, 0x020003, 0x040005
        };

        /// <summary>
        /// Decodes a number in the range [0..255], by reading 1 - 11 bits.
        /// </summary>
        private static int DecodeVarLenUnsignedByte(BitReader br)
        {
            if (BitReader.ReadBits(br, 1) != 0)
            {
                int n = BitReader.ReadBits(br, 3);
                if (n == 0)
                {
                    return 1;
                }
                else
                {
                    return BitReader.ReadBits(br, n) + (1 << n);
                }
            }
            return 0;
        }

        private static void DecodeMetaBlockLength(BitReader br, State state)
        {
            state.inputEnd = BitReader.ReadBits(br, 1) == 1;
            state.metaBlockLength = 0;
            state.isUncompressed = false;
            state.isMetadata = false;
            if (state.inputEnd && BitReader.ReadBits(br, 1) != 0)
            {
                return;
            }
            int sizeNibbles = BitReader.ReadBits(br, 2) + 4;
            if (sizeNibbles == 7)
            {
                state.isMetadata = true;
                if (BitReader.ReadBits(br, 1) != 0)
                {
                    throw new BrotliRuntimeException("Corrupted reserved bit");
                }
                int sizeBytes = BitReader.ReadBits(br, 2);
                if (sizeBytes == 0)
                {
                    return;
                }
                for (int i = 0; i < sizeBytes; i++)
                {
                    int bits = BitReader.ReadBits(br, 8);
                    if (bits == 0 && i + 1 == sizeBytes && sizeBytes > 1)
                    {
                        throw new BrotliRuntimeException("Exuberant nibble");
                    }
                    state.metaBlockLength |= bits << (i * 8);
                }
            }
            else
            {
                for (int i = 0; i < sizeNibbles; i++)
                {
                    int bits = BitReader.ReadBits(br, 4);
                    if (bits == 0 && i + 1 == sizeNibbles && sizeNibbles > 4)
                    {
                        throw new BrotliRuntimeException("Exuberant nibble");
                    }
                    state.metaBlockLength |= bits << (i * 4);
                }
            }
            state.metaBlockLength++;
            if (!state.inputEnd)
            {
                state.isUncompressed = BitReader.ReadBits(br, 1) == 1;
            }
        }

        private static int ReadSymbol(int[] table, int offset, BitReader br)
        {
            BitReader.FillBitWindow(br);
            offset += (int)((ulong)br.accumulator >> br.bitOffset) & HUFFMAN_TABLE_MASK;
            int n = (table[offset] >> 16) - HUFFMAN_TABLE_BITS;
            if (n > 0)
            {
                br.bitOffset += HUFFMAN_TABLE_BITS;
                offset += table[offset] & 0xFFFF;
                offset += (int)((ulong)br.accumulator >> br.bitOffset) & ((1 << n) - 1);
            }
            br.bitOffset += table[offset] >> 16;
            return table[offset] & 0xFFFF;
        }

        private static int ReadBlockLength(int[] table, int offset, BitReader br)
        {
            int code = ReadSymbol(table, offset, br);
            int n = Prefix.BLOCK_LENGTH_N_BITS[code];
            return Prefix.BLOCK_LENGTH_OFFSET[code] + BitReader.ReadBits(br, n);
        }

        private static int TranslateShortCodes(int code, int[] ringBuffer, int index)
        {
            if (code < NUM_DISTANCE_SHORT_CODES)
            {
                index += DISTANCE_SHORT_CODE_INDEX_OFFSET[code];
                index &= 3;
                return ringBuffer[index] + DISTANCE_SHORT_CODE_VALUE_OFFSET[code];
            }
            return code - NUM_DISTANCE_SHORT_CODES + 1;
        }

        private static void MoveToFront(int[] v, int index)
        {
            int value = v[index];
            for (; index > 0; index--)
            {
                v[index] = v[index - 1];
            }
            v[0] = value;
        }

        private static void InverseMoveToFrontTransform(byte[] v, int vLen)
        {
            int[] mtf = new int[256];
            for (int i = 0; i < 256; i++)
            {
                mtf[i] = i;
            }
            for (int i = 0; i < vLen; i++)
            {
                int index = v[i] & 0xFF;
                v[i] = (byte)mtf[index];
                if (index != 0)
                {
                    MoveToFront(mtf, index);
                }
            }
        }

        private static void ReadHuffmanCodeLengths(
      int[] codeLengthCodeLengths, int numSymbols, int[] codeLengths, BitReader br)
        {
            int symbol = 0;
            int prevCodeLen = DEFAULT_CODE_LENGTH;
            int repeat = 0;
            int repeatCodeLen = 0;
            int space = 32768;
            int[] table = new int[32];

            Huffman.BuildHuffmanTable(table, 0, 5, codeLengthCodeLengths, CODE_LENGTH_CODES);

            while (symbol < numSymbols && space > 0)
            {
                BitReader.ReadMoreInput(br);
                BitReader.FillBitWindow(br);
                int p = (int)(((ulong)br.accumulator >> br.bitOffset)) & 31;
                br.bitOffset += table[p] >> 16;
                int codeLen = table[p] & 0xFFFF;
                if (codeLen < CODE_LENGTH_REPEAT_CODE)
                {
                    repeat = 0;
                    codeLengths[symbol++] = codeLen;
                    if (codeLen != 0)
                    {
                        prevCodeLen = codeLen;
                        space -= 32768 >> codeLen;
                    }
                }
                else
                {
                    int extraBits = codeLen - 14;
                    int newLen = 0;
                    if (codeLen == CODE_LENGTH_REPEAT_CODE)
                    {
                        newLen = prevCodeLen;
                    }
                    if (repeatCodeLen != newLen)
                    {
                        repeat = 0;
                        repeatCodeLen = newLen;
                    }
                    int oldRepeat = repeat;
                    if (repeat > 0)
                    {
                        repeat -= 2;
                        repeat <<= extraBits;
                    }
                    repeat += BitReader.ReadBits(br, extraBits) + 3;
                    int repeatDelta = repeat - oldRepeat;
                    if (symbol + repeatDelta > numSymbols)
                    {
                        throw new BrotliRuntimeException("symbol + repeatDelta > numSymbols"); // COV_NF_LINE
                    }
                    for (int i = 0; i < repeatDelta; i++)
                    {
                        codeLengths[symbol++] = repeatCodeLen;
                    }
                    if (repeatCodeLen != 0)
                    {
                        space -= repeatDelta << (15 - repeatCodeLen);
                    }
                }
            }
            if (space != 0)
            {
                throw new BrotliRuntimeException("Unused space"); // COV_NF_LINE
            }
            // TODO: Pass max_symbol to Huffman table builder instead?
            Utils.FillWithZeroes(codeLengths, symbol, numSymbols - symbol);
        }

        public static void ReadHuffmanCode(int alphabetSize, int[] table, int offset, BitReader br)
        {
            bool ok = true;
            int simpleCodeOrSkip;
            BitReader.ReadMoreInput(br);
            // TODO: Avoid allocation.
            int[] codeLengths = new int[alphabetSize];
            simpleCodeOrSkip = BitReader.ReadBits(br, 2);
            if (simpleCodeOrSkip == 1)
            { // Read symbols, codes & code lengths directly.
                int maxBitsCounter = alphabetSize - 1;
                int maxBits = 0;
                int[] symbols = new int[4];
                int numSymbols = BitReader.ReadBits(br, 2) + 1;
                while (maxBitsCounter != 0)
                {
                    maxBitsCounter >>= 1;
                    maxBits++;
                }
                Utils.FillWithZeroes(codeLengths, 0, alphabetSize);
                for (int i = 0; i < numSymbols; i++)
                {
                    symbols[i] = BitReader.ReadBits(br, maxBits) % alphabetSize;
                    codeLengths[symbols[i]] = 2;
                }
                codeLengths[symbols[0]] = 1;
                switch (numSymbols)
                {
                    case 1:
                        break;
                    case 2:
                        ok = symbols[0] != symbols[1];
                        codeLengths[symbols[1]] = 1;
                        break;
                    case 3:
                        ok = symbols[0] != symbols[1] && symbols[0] != symbols[2] && symbols[1] != symbols[2];
                        break;
                    case 4:
                        ok = symbols[0] != symbols[1] && symbols[0] != symbols[2] && symbols[0] != symbols[3]
                            && symbols[1] != symbols[2] && symbols[1] != symbols[3] && symbols[2] != symbols[3];
                        if (BitReader.ReadBits(br, 1) == 1)
                        {
                            codeLengths[symbols[2]] = 3;
                            codeLengths[symbols[3]] = 3;
                        }
                        else
                        {
                            codeLengths[symbols[0]] = 2;
                        }
                        break;
                }
            }
            else
            { // Decode Huffman-coded code lengths.
                int[] codeLengthCodeLengths = new int[CODE_LENGTH_CODES];
                int space = 32;
                int numCodes = 0;
                for (int i = simpleCodeOrSkip; i < CODE_LENGTH_CODES && space > 0; i++)
                {
                    int codeLenIdx = CODE_LENGTH_CODE_ORDER[i];
                    BitReader.FillBitWindow(br);
                    int p = (int)((ulong)br.accumulator >> br.bitOffset) & 15;
                    // TODO: Demultiplex FIXED_TABLE.
                    br.bitOffset += FIXED_TABLE[p] >> 16;
                    int v = FIXED_TABLE[p] & 0xFFFF;
                    codeLengthCodeLengths[codeLenIdx] = v;
                    if (v != 0)
                    {
                        space -= (32 >> v);
                        numCodes++;
                    }
                }
                ok = (numCodes == 1 || space == 0);
                ReadHuffmanCodeLengths(codeLengthCodeLengths, alphabetSize, codeLengths, br);
            }
            if (!ok)
            {
                throw new BrotliRuntimeException("Can't readHuffmanCode"); // COV_NF_LINE
            }
            Huffman.BuildHuffmanTable(table, offset, HUFFMAN_TABLE_BITS, codeLengths, alphabetSize);
        }

        private static int DecodeContextMap(int contextMapSize, byte[] contextMap, BitReader br)
        {
            BitReader.ReadMoreInput(br);
            int numTrees = DecodeVarLenUnsignedByte(br) + 1;

            if (numTrees == 1)
            {
                Utils.FillWithZeroes(contextMap, 0, contextMapSize);
                return numTrees;
            }

            bool useRleForZeros = BitReader.ReadBits(br, 1) == 1;
            int maxRunLengthPrefix = 0;
            if (useRleForZeros)
            {
                maxRunLengthPrefix = BitReader.ReadBits(br, 4) + 1;
            }
            int[] table = new int[Huffman.HUFFMAN_MAX_TABLE_SIZE];
            ReadHuffmanCode(numTrees + maxRunLengthPrefix, table, 0, br);
            for (int i = 0; i < contextMapSize;)
            {
                BitReader.ReadMoreInput(br);
                int code = ReadSymbol(table, 0, br);
                if (code == 0)
                {
                    contextMap[i] = 0;
                    i++;
                }
                else if (code <= maxRunLengthPrefix)
                {
                    int reps = (1 << code) + BitReader.ReadBits(br, code);
                    while (reps != 0)
                    {
                        if (i >= contextMapSize)
                        {
                            throw new BrotliRuntimeException("Corrupted context map"); // COV_NF_LINE
                        }
                        contextMap[i] = 0;
                        i++;
                        reps--;
                    }
                }
                else
                {
                    contextMap[i] = (byte)(code - maxRunLengthPrefix);
                    i++;
                }
            }
            if (BitReader.ReadBits(br, 1) == 1)
            {
                InverseMoveToFrontTransform(contextMap, contextMapSize);
            }
            return numTrees;
        }

        private static void DecodeBlockTypeAndLength(State state, int treeType)
        {
            BitReader br = state.br;
            int[] ringBuffers = state.blockTypeRb;
            int offset = treeType * 2;
            int blockType = ReadSymbol(state.blockTypeTrees, treeType * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);
            state.blockLength[treeType] = ReadBlockLength(state.blockLenTrees,
                treeType * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);

            if (blockType == 1)
            {
                blockType = ringBuffers[offset + 1] + 1;
            }
            else if (blockType == 0)
            {
                blockType = ringBuffers[offset];
            }
            else
            {
                blockType -= 2;
            }
            if (blockType >= state.numBlockTypes[treeType])
            {
                blockType -= state.numBlockTypes[treeType];
            }
            ringBuffers[offset] = ringBuffers[offset + 1];
            ringBuffers[offset + 1] = blockType;
        }

        private static void DecodeLiteralBlockSwitch(State state)
        {
            DecodeBlockTypeAndLength(state, 0);
            int literalBlockType = state.blockTypeRb[1];
            state.contextMapSlice = literalBlockType << LITERAL_CONTEXT_BITS;
            state.literalTreeIndex = state.contextMap[state.contextMapSlice] & 0xFF;
            state.literalTree = state.hGroup0.trees[state.literalTreeIndex];
            int contextMode = state.contextModes[literalBlockType];
            state.contextLookupOffset1 = Context.LOOKUP_OFFSETS[contextMode];
            state.contextLookupOffset2 = Context.LOOKUP_OFFSETS[contextMode + 1];
        }

        private static void DecodeCommandBlockSwitch(State state)
        {
            DecodeBlockTypeAndLength(state, 1);
            state.treeCommandOffset = state.hGroup1.trees[state.blockTypeRb[3]];
        }

        private static void DecodeDistanceBlockSwitch(State state)
        {
            DecodeBlockTypeAndLength(state, 2);
            state.distContextMapSlice = state.blockTypeRb[5] << DISTANCE_CONTEXT_BITS;
        }

        public static void MaybeReallocateRingBuffer(State state)
        {
            int newSize = state.maxRingBufferSize;
            if ((long)newSize > state.expectedTotalSize)
            {
                /* TODO: Handle 2GB+ cases more gracefully. */
                int minimalNewSize = (int)state.expectedTotalSize + state.customDictionary.Length;
                while ((newSize >> 1) > minimalNewSize)
                {
                    newSize >>= 1;
                }
                if (!state.inputEnd && newSize < 16384 && state.maxRingBufferSize >= 16384)
                {
                    newSize = 16384;
                }
            }
            if (newSize <= state.ringBufferSize)
            {
                return;
            }
            int ringBufferSizeWithSlack = newSize + Dictionary.MAX_TRANSFORMED_WORD_LENGTH;
            byte[] newBuffer = new byte[ringBufferSizeWithSlack];
            if (state.ringBuffer != null)
            {
                Array.Copy(state.ringBuffer, 0, newBuffer, 0, state.ringBufferSize);
            }
            else
            {
                /* Prepend custom dictionary, if any. */
                if (state.customDictionary.Length != 0)
                {
                    int length = state.customDictionary.Length;
                    int offset = 0;
                    if (length > state.maxBackwardDistance)
                    {
                        offset = length - state.maxBackwardDistance;
                        length = state.maxBackwardDistance;
                    }
                    Array.Copy(state.customDictionary, offset, newBuffer, 0, length);
                    state.pos = length;
                    state.bytesToIgnore = length;
                }
            }
            state.ringBuffer = newBuffer;
            state.ringBufferSize = newSize;
        }

        /// <summary>
        /// Reads next metablock header.
        /// </summary>
        /// <param name="state">decoding state</param>
        public static void ReadMeablockInfo(State state)
        {
            BitReader br = state.br;

            if (state.inputEnd)
            {
                state.nextRunningState = RunningStage.FINISHED;
                state.bytesToWrite = state.pos & (state.ringBufferSize - 1);
                state.bytesWritten = 0;
                state.runningState = RunningStage.WRITE;
                return;
            }
            // TODO: Reset? Do we need this?
            state.hGroup0.codes = null;
            state.hGroup0.trees = null;
            state.hGroup1.codes = null;
            state.hGroup1.trees = null;
            state.hGroup2.codes = null;
            state.hGroup2.trees = null;

            BitReader.ReadMoreInput(br);
            DecodeMetaBlockLength(br, state);
            if (state.metaBlockLength == 0 && !state.isMetadata)
            {
                return;
            }
            if (state.isUncompressed || state.isMetadata)
            {
                BitReader.JumpToByteBoundry(br);
                state.runningState = state.isMetadata ? RunningStage.READ_METADATA : RunningStage.COPY_UNCOMPRESSED;
            }
            else
            {
                state.runningState = RunningStage.COMPRESSED_BLOCK_START;
            }

            if (state.isMetadata)
            {
                return;
            }
            state.expectedTotalSize += state.metaBlockLength;
            if (state.ringBufferSize < state.maxRingBufferSize)
            {
                MaybeReallocateRingBuffer(state);
            }
        }

        public static void ReadMetablockHuffmanCodesAndContextMaps(State state)
        {
            BitReader br = state.br;

            for (int i = 0; i < 3; i++)
            {
                state.numBlockTypes[i] = DecodeVarLenUnsignedByte(br) + 1;
                state.blockLength[i] = 1 << 28;
                if (state.numBlockTypes[i] > 1)
                {
                    ReadHuffmanCode(state.numBlockTypes[i] + 2, state.blockTypeTrees,
                        i * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);
                    ReadHuffmanCode(NUM_BLOCK_LENGTH_CODES, state.blockLenTrees,
                        i * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);
                    state.blockLength[i] = ReadBlockLength(state.blockLenTrees,
                        i * Huffman.HUFFMAN_MAX_TABLE_SIZE, br);
                }
            }

            BitReader.ReadMoreInput(br);
            state.distancePostfixBits = BitReader.ReadBits(br, 2);
            state.numDirectDistanceCodes =
                NUM_DISTANCE_SHORT_CODES + (BitReader.ReadBits(br, 4) << state.distancePostfixBits);
            state.distancePostfixMask = (1 << state.distancePostfixBits) - 1;
            int numDistanceCodes = state.numDirectDistanceCodes + (48 << state.distancePostfixBits);
            // TODO: Reuse?
            state.contextModes = new byte[state.numBlockTypes[0]];
            for (int i = 0; i < state.numBlockTypes[0];)
            {
                /* Ensure that less than 256 bits read between readMoreInput. */
                int limit = Math.Min(i + 96, state.numBlockTypes[0]);
                for (; i < limit; ++i)
                {
                    state.contextModes[i] = (byte)(BitReader.ReadBits(br, 2) << 1);
                }
                BitReader.ReadMoreInput(br);
            }

            // TODO: Reuse?
            state.contextMap = new byte[state.numBlockTypes[0] << LITERAL_CONTEXT_BITS];
            int numLiteralTrees = DecodeContextMap(state.numBlockTypes[0] << LITERAL_CONTEXT_BITS,
                state.contextMap, br);
            state.trivialLiteralContext = true;
            for (int j = 0; j < state.numBlockTypes[0] << LITERAL_CONTEXT_BITS; j++)
            {
                if (state.contextMap[j] != j >> LITERAL_CONTEXT_BITS)
                {
                    state.trivialLiteralContext = false;
                    break;
                }
            }

            // TODO: Reuse?
            state.distContextMap = new byte[state.numBlockTypes[2] << DISTANCE_CONTEXT_BITS];
            int numDistTrees = DecodeContextMap(state.numBlockTypes[2] << DISTANCE_CONTEXT_BITS,
                state.distContextMap, br);

            HuffmanTreeGroup.Init(state.hGroup0, NUM_LITERAL_CODES, numLiteralTrees);
            HuffmanTreeGroup.Init(state.hGroup1, NUM_INSERT_AND_COPY_CODES, state.numBlockTypes[1]);
            HuffmanTreeGroup.Init(state.hGroup2, numDistanceCodes, numDistTrees);

            HuffmanTreeGroup.Decode(state.hGroup0, br);
            HuffmanTreeGroup.Decode(state.hGroup1, br);
            HuffmanTreeGroup.Decode(state.hGroup2, br);

            state.contextMapSlice = 0;
            state.distContextMapSlice = 0;
            state.contextLookupOffset1 = Context.LOOKUP_OFFSETS[state.contextModes[0]];
            state.contextLookupOffset2 = Context.LOOKUP_OFFSETS[state.contextModes[0] + 1];
            state.literalTreeIndex = 0;
            state.literalTree = state.hGroup0.trees[0];
            state.treeCommandOffset = state.hGroup1.trees[0]; // TODO: == 0?

            state.blockTypeRb[0] = state.blockTypeRb[2] = state.blockTypeRb[4] = 1;
            state.blockTypeRb[1] = state.blockTypeRb[3] = state.blockTypeRb[5] = 0;
        }

        public static void CopyUncompressedData(State state)
        {
            BitReader br = state.br;
            byte[] ringBuffer = state.ringBuffer;
            int ringBufferMask = state.ringBufferSize - 1;

            while (state.metaBlockLength > 0)
            {
                BitReader.ReadMoreInput(br);
                // Optimize
                ringBuffer[state.pos & ringBufferMask] = (byte)(BitReader.ReadBits(br, 8));
                state.metaBlockLength--;
                if ((state.pos++ & ringBufferMask) == ringBufferMask)
                {
                    state.nextRunningState = RunningStage.COPY_UNCOMPRESSED;
                    state.bytesToWrite = state.ringBufferSize;
                    state.bytesWritten = 0;
                    state.runningState = RunningStage.WRITE;
                    return;
                }
            }
            state.runningState = RunningStage.BLOCK_START;
        }

        public static bool WriteRingBuffer(State state)
        {
            /* Ignore custom dictionary bytes. */
            if (state.bytesToIgnore != 0)
            {
                state.bytesWritten += state.bytesToIgnore;
                state.bytesToIgnore = 0;
            }
            int toWrite = Math.Min(state.outputLength - state.outputUsed,
                state.bytesToWrite - state.bytesWritten);
            if (toWrite != 0)
            {
                Array.Copy(state.ringBuffer, state.bytesWritten, state.output,
                    state.outputOffset + state.outputUsed, toWrite);
                state.outputUsed += toWrite;
                state.bytesWritten += toWrite;
            }

            return state.outputUsed < state.outputLength;
        }

        public static void SetCustomDictionary(State state, byte[] data)
        {
            state.customDictionary = (data == null) ? new byte[0] : data;
        }

        public static void Decompress(State state)
        {
            if (state.runningState == RunningStage.UNINITIALIZED)
            {
                throw new InvalidOperationException("Can't decompress until initialized");
            }
            if (state.runningState == RunningStage.CLOSED)
            {
                throw new InvalidOperationException("Can't decompress after close");
            }
            BitReader br = state.br;
            int ringBufferMask = state.ringBufferSize - 1;
            byte[] ringBuffer = state.ringBuffer;

            while (state.runningState != RunningStage.FINISHED)
            {
                // TODO: extract cases to methods for the better readability.
                switch (state.runningState)
                {
                    case RunningStage.BLOCK_START:
                        if (state.metaBlockLength < 0)
                        {
                            throw new BrotliRuntimeException("Invalid metablock length");
                        }
                        ReadMeablockInfo(state);
                        /* Ring-buffer would be reallocated here. */
                        ringBufferMask = state.ringBufferSize - 1;
                        ringBuffer = state.ringBuffer;
                        continue;

                    case RunningStage.COMPRESSED_BLOCK_START:
                        ReadMetablockHuffmanCodesAndContextMaps(state);
                        state.runningState = RunningStage.MAIN_LOOP;
                        goto case RunningStage.MAIN_LOOP;
                    // Fall through

                    case RunningStage.MAIN_LOOP:
                        if (state.metaBlockLength <= 0)
                        {
                            // Protect pos from overflow, wrap it around at every GB of input data.
                            state.pos &= 0x3fffffff;
                            state.runningState = RunningStage.BLOCK_START;
                            continue;
                        }
                        BitReader.ReadMoreInput(br);
                        if (state.blockLength[1] == 0)
                        {
                            DecodeCommandBlockSwitch(state);
                        }
                        state.blockLength[1]--;
                        int cmdCode = ReadSymbol(state.hGroup1.codes, state.treeCommandOffset, br);
                        int rangeIdx = (int)((uint)cmdCode >> 6);
                        state.distanceCode = 0;
                        if (rangeIdx >= 2)
                        {
                            rangeIdx -= 2;
                            state.distanceCode = -1;
                        }
                        int insertCode = Prefix.INSERT_RANGE_LUT[rangeIdx] + ((int)((uint)cmdCode >> 3) & 7);
                        int copyCode = Prefix.COPY_RANGE_LUT[rangeIdx] + (cmdCode & 7);
                        state.insertLength = Prefix.INSERT_LENGTH_OFFSET[insertCode] + BitReader
                            .ReadBits(br, Prefix.INSERT_LENGTH_N_BITS[insertCode]);
                        state.copyLength = Prefix.COPY_LENGTH_OFFSET[copyCode] + BitReader
                            .ReadBits(br, Prefix.COPY_LENGTH_N_BITS[copyCode]);

                        state.j = 0;
                        state.runningState = RunningStage.INSERT_LOOP;
                        goto case RunningStage.INSERT_LOOP;
                    // Fall through
                    case RunningStage.INSERT_LOOP:
                        if (state.trivialLiteralContext)
                        {
                            while (state.j < state.insertLength)
                            {
                                BitReader.ReadMoreInput(br);
                                if (state.blockLength[0] == 0)
                                {
                                    DecodeLiteralBlockSwitch(state);
                                }
                                state.blockLength[0]--;
                                ringBuffer[state.pos & ringBufferMask] = (byte)ReadSymbol(
                                    state.hGroup0.codes, state.literalTree, br);
                                state.j++;
                                if ((state.pos++ & ringBufferMask) == ringBufferMask)
                                {
                                    state.nextRunningState = RunningStage.INSERT_LOOP;
                                    state.bytesToWrite = state.ringBufferSize;
                                    state.bytesWritten = 0;
                                    state.runningState = RunningStage.WRITE;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            int prevByte1 = ringBuffer[(state.pos - 1) & ringBufferMask] & 0xFF;
                            int prevByte2 = ringBuffer[(state.pos - 2) & ringBufferMask] & 0xFF;
                            while (state.j < state.insertLength)
                            {
                                BitReader.ReadMoreInput(br);
                                if (state.blockLength[0] == 0)
                                {
                                    DecodeLiteralBlockSwitch(state);
                                }
                                int literalTreeIndex = state.contextMap[state.contextMapSlice
                                  + (Context.LOOKUP[state.contextLookupOffset1 + prevByte1]
                                      | Context.LOOKUP[state.contextLookupOffset2 + prevByte2])] & 0xFF;
                                state.blockLength[0]--;
                                prevByte2 = prevByte1;
                                prevByte1 = ReadSymbol(
                                    state.hGroup0.codes, state.hGroup0.trees[literalTreeIndex], br);
                                ringBuffer[state.pos & ringBufferMask] = (byte)prevByte1;
                                state.j++;
                                if ((state.pos++ & ringBufferMask) == ringBufferMask)
                                {
                                    state.nextRunningState = RunningStage.INSERT_LOOP;
                                    state.bytesToWrite = state.ringBufferSize;
                                    state.bytesWritten = 0;
                                    state.runningState = RunningStage.WRITE;
                                    break;
                                }
                            }
                        }
                        if (state.runningState != RunningStage.INSERT_LOOP)
                        {
                            continue;
                        }
                        state.metaBlockLength -= state.insertLength;
                        if (state.metaBlockLength <= 0)
                        {
                            state.runningState = RunningStage.MAIN_LOOP;
                            continue;
                        }
                        if (state.distanceCode < 0)
                        {
                            BitReader.ReadMoreInput(br);
                            if (state.blockLength[2] == 0)
                            {
                                DecodeDistanceBlockSwitch(state);
                            }
                            state.blockLength[2]--;
                            state.distanceCode = ReadSymbol(state.hGroup2.codes, state.hGroup2.trees[
                                state.distContextMap[state.distContextMapSlice
                                    + (state.copyLength > 4 ? 3 : state.copyLength - 2)] & 0xFF], br);
                            if (state.distanceCode >= state.numDirectDistanceCodes)
                            {
                                state.distanceCode -= state.numDirectDistanceCodes;
                                int postfix = state.distanceCode & state.distancePostfixMask;
                                //state.distanceCode >>>= state.distancePostfixBits;
                                state.distanceCode = (int)((uint)state.distanceCode >> state.distancePostfixBits);
                                int n = (int)((uint)state.distanceCode >> 1) + 1;
                                int offset = ((2 + (state.distanceCode & 1)) << n) - 4;
                                state.distanceCode = state.numDirectDistanceCodes + postfix
                                    + ((offset + BitReader.ReadBits(br, n)) << state.distancePostfixBits);
                            }
                        }

                        // Convert the distance code to the actual distance by possibly looking up past distances
                        // from the ringBuffer.
                        state.distance = TranslateShortCodes(state.distanceCode, state.distRb, state.distRbIdx);
                        if (state.distance < 0)
                        {
                            throw new BrotliRuntimeException("Negative distance"); // COV_NF_LINE
                        }

                        if (state.pos < state.maxBackwardDistance
                            && state.maxDistance != state.maxBackwardDistance)
                        {
                            state.maxDistance = state.pos;
                        }
                        else
                        {
                            state.maxDistance = state.maxBackwardDistance;
                        }

                        state.copyDst = state.pos & ringBufferMask;
                        if (state.distance > state.maxDistance)
                        {
                            state.runningState = RunningStage.TRANSFORM;
                            continue;
                        }

                        if (state.distanceCode > 0)
                        {
                            state.distRb[state.distRbIdx & 3] = state.distance;
                            state.distRbIdx++;
                        }

                        if (state.copyLength > state.metaBlockLength)
                        {
                            throw new BrotliRuntimeException("Invalid backward reference"); // COV_NF_LINE
                        }
                        state.j = 0;
                        state.runningState = RunningStage.COPY_LOOP;
                        goto case RunningStage.COPY_LOOP;
                    // fall through
                    case RunningStage.COPY_LOOP:
                        for (; state.j < state.copyLength;)
                        {
                            ringBuffer[state.pos & ringBufferMask] =
                                ringBuffer[(state.pos - state.distance) & ringBufferMask];
                            // TODO: condense
                            state.metaBlockLength--;
                            state.j++;
                            if ((state.pos++ & ringBufferMask) == ringBufferMask)
                            {
                                state.nextRunningState = RunningStage.COPY_LOOP;
                                state.bytesToWrite = state.ringBufferSize;
                                state.bytesWritten = 0;
                                state.runningState = RunningStage.WRITE;
                                break;
                            }
                        }
                        if (state.runningState == RunningStage.COPY_LOOP)
                        {
                            state.runningState = RunningStage.MAIN_LOOP;
                        }
                        continue;

                    case RunningStage.TRANSFORM:
                        if (state.copyLength >= Dictionary.MIN_WORD_LENGTH
                            && state.copyLength <= Dictionary.MAX_WORD_LENGTH)
                        {
                            int offset = Dictionary.OFFSETS_BY_LENGTH[state.copyLength];
                            int wordId = state.distance - state.maxDistance - 1;
                            int shift = Dictionary.SIZE_BITS_BY_LENGTH[state.copyLength];
                            int mask = (1 << shift) - 1;
                            int wordIdx = wordId & mask;
                            int transformIdx = (int)((uint)wordId >> shift);
                            offset += wordIdx * state.copyLength;
                            if (transformIdx < Transform.TRANSFORMS.Length)
                            {
                                int len = Transform.TransformDictionaryWord(ringBuffer, state.copyDst,
                                    Dictionary.GetData(), offset, state.copyLength,
                                    Transform.TRANSFORMS[transformIdx]);
                                state.copyDst += len;
                                state.pos += len;
                                state.metaBlockLength -= len;
                                if (state.copyDst >= state.ringBufferSize)
                                {
                                    state.nextRunningState = RunningStage.COPY_WRAP_BUFFER;
                                    state.bytesToWrite = state.ringBufferSize;
                                    state.bytesWritten = 0;
                                    state.runningState = RunningStage.WRITE;
                                    continue;
                                }
                            }
                            else
                            {
                                throw new BrotliRuntimeException("Invalid backward reference"); // COV_NF_LINE
                            }
                        }
                        else
                        {
                            throw new BrotliRuntimeException("Invalid backward reference"); // COV_NF_LINE
                        }
                        state.runningState = RunningStage.MAIN_LOOP;
                        continue;

                    case RunningStage.COPY_WRAP_BUFFER:
                        Array.Copy(ringBuffer, state.ringBufferSize, ringBuffer, 0,
                            state.copyDst - state.ringBufferSize);
                        state.runningState = RunningStage.MAIN_LOOP;
                        continue;

                    case RunningStage.READ_METADATA:
                        while (state.metaBlockLength > 0)
                        {
                            BitReader.ReadMoreInput(br);
                            // Optimize
                            BitReader.ReadBits(br, 8);
                            state.metaBlockLength--;
                        }
                        state.runningState = RunningStage.BLOCK_START;
                        continue;


                    case RunningStage.COPY_UNCOMPRESSED:
                        CopyUncompressedData(state);
                        continue;

                    case RunningStage.WRITE:
                        if (!WriteRingBuffer(state))
                        {
                            // Output buffer is full.
                            return;
                        }
                        state.runningState = state.nextRunningState;
                        continue;

                    default:
                        throw new BrotliRuntimeException("Unexpected state " + state.runningState);
                }
            }
            if (state.runningState == RunningStage.FINISHED)
            {
                if (state.metaBlockLength < 0)
                {
                    throw new BrotliRuntimeException("Invalid metablock length");
                }
                BitReader.JumpToByteBoundry(br);
                BitReader.CheckHealth(state.br);
            }
        }
    }
}