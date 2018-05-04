using System;
using System.IO;

namespace CSharpBrotli.Decode
{
    public sealed class State
    {
        public RunningStage runningState = RunningStage.UNINITIALIZED;
        public RunningStage nextRunningState;
        public readonly BitReader br = new BitReader();
        public byte[] ringBuffer;
        public readonly int[] blockTypeTrees = new int[3 * Huffman.HUFFMAN_MAX_TABLE_SIZE];
        public readonly int[] blockLenTrees = new int[3 * Huffman.HUFFMAN_MAX_TABLE_SIZE];

        // Current meta-block header information.
        public int metaBlockLength;
        public bool inputEnd;
        public bool isUncompressed;
        public bool isMetadata;

        public readonly HuffmanTreeGroup hGroup0 = new HuffmanTreeGroup();
        public readonly HuffmanTreeGroup hGroup1 = new HuffmanTreeGroup();
        public readonly HuffmanTreeGroup hGroup2 = new HuffmanTreeGroup();
        public readonly int[] blockLength = new int[3];
        public readonly int[] numBlockTypes = new int[3];
        public readonly int[] blockTypeRb = new int[6];
        public readonly int[] distRb = { 16, 15, 11, 4 };
        public int pos = 0;
        public int maxDistance = 0;
        public int distRbIdx = 0;
        public bool trivialLiteralContext = false;
        public int literalTreeIndex = 0;
        public int literalTree;
        public int j;
        public int insertLength;
        public byte[] contextModes;
        public byte[] contextMap;
        public int contextMapSlice;
        public int distContextMapSlice;
        public int contextLookupOffset1;
        public int contextLookupOffset2;
        public int treeCommandOffset;
        public int distanceCode;
        public byte[] distContextMap;
        public int numDirectDistanceCodes;
        public int distancePostfixMask;
        public int distancePostfixBits;
        public int distance;
        public int copyLength;
        public int copyDst;
        public int maxBackwardDistance;
        public int maxRingBufferSize;
        public int ringBufferSize = 0;
        public long expectedTotalSize = 0;
        public byte[] customDictionary = new byte[0];
        public int bytesToIgnore = 0;

        public int outputOffset;
        public int outputLength;
        public int outputUsed;
        public int bytesWritten;
        public int bytesToWrite;
        public byte[] output;

        private static int DecodeWindowBits(BitReader br)
        {
            if (BitReader.ReadBits(br, 1) == 0)
            {
                return 16;
            }
            int n = BitReader.ReadBits(br, 3);
            if (n != 0)
            {
                return 17 + n;
            }
            n = BitReader.ReadBits(br, 3);
            if (n != 0)
            {
                return 8 + n;
            }
            return 17;
        }

        public static void SetInput(State state, Stream input)
        {
            if (state.runningState != RunningStage.UNINITIALIZED)
            {
                throw new InvalidOperationException("State MUST be uninitialized");
            }
            BitReader.Init(state.br, input);
            int windowBits = DecodeWindowBits(state.br);
            if (windowBits == 9)
            { /* Reserved case for future expansion. */
                throw new BrotliRuntimeException("Invalid 'windowBits' code");
            }
            state.maxRingBufferSize = 1 << windowBits;
            state.maxBackwardDistance = state.maxRingBufferSize - 16;
            state.runningState = RunningStage.BLOCK_START;
        }

        public static void Close(State state)
        {
            try
            {
                if (state.runningState == RunningStage.UNINITIALIZED)
                {
                    throw new InvalidOperationException("State MUST be initialized");
                }
                if (state.runningState == RunningStage.CLOSED)
                {
                    return;
                }
                state.runningState = RunningStage.CLOSED;
                BitReader.Close(state.br);
            }
            catch(IOException ex)
            {
                throw ex;
            }
        }
    }
}