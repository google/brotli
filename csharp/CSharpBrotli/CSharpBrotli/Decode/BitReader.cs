using System;
using System.IO;

namespace CSharpBrotli.Decode
{
    /// <summary>
    /// Bit reading helpers.
    /// </summary>
    public class BitReader
    {
        /// <summary>
        /// Input byte buffer, consist of a ring-buffer and a "slack" region where bytes from the start of
        /// the ring-buffer are copied.
        /// </summary>
        private const int READ_SIZE = 4096;
        private const int BUF_SIZE = READ_SIZE + 64;

        //private const ByteBuffer byteBuffer = ByteBuffer.allocateDirect(BUF_SIZE).order(ByteOrder.LITTLE_ENDIAN);
        //private const IntBuffer intBuffer = byteBuffer.asIntBuffer();
        private MemoryStream byteBuffer;// = CreatesStream();
        private IntBufferReader intBuffer;// = new BinaryReader(byteBuffer);
        private byte[] shadowBuffer = new byte[BUF_SIZE];

        private Stream input;

        /// <summary>
        /// Input stream is finished.
        /// </summary>
        private bool endOfStreamReached;

        private long acc;
        /// <summary>
        /// Pre-fetched bits.
        /// </summary>
        public long accumulator
        {
            get
            {
                return acc;
            }
            set
            {
                acc = value;
            }
        }

        /// <summary>
        /// Current bit-reading position in accumulator.
        /// </summary>
        public int bitOffset;

        /// <summary>
        /// Number of 32-bit integers availabale for reading.
        /// </summary>
        private int available;

        private int tailBytes = 0;

        public BitReader()
        {
            byteBuffer = new MemoryStream(BUF_SIZE);
            intBuffer = new IntBufferReader(byteBuffer);
        }

        /// <summary>
        /// Fills up the input buffer.
        /// <para>No-op if there are at least 36 bytes present after current position.</para>
        /// <para>After encountering the end of the input stream, 64 additional zero bytes are copied 
        /// to the buffer.</para>
        /// </summary>
        public static void ReadMoreInput(BitReader br)
        {
            if (br.available > 9)
            {
                return;
            }
            if (br.endOfStreamReached)
            {
                if (br.available > 4)
                {
                    return;
                }
                throw new BrotliRuntimeException("No more input");
            }
            int readOffset = (int)(br.intBuffer.Position << 2);
            int bytesRead = READ_SIZE - readOffset;
            Array.Copy(br.shadowBuffer, readOffset, br.shadowBuffer, 0, bytesRead);
            try
            {
                while (bytesRead < READ_SIZE)
                {
                    int len = br.input.Read(br.shadowBuffer, bytesRead, READ_SIZE - bytesRead);
                    if (len <= 0)
                    {
                        br.endOfStreamReached = true;
                        Utils.FillWithZeroes(br.shadowBuffer, bytesRead, 64);
                        bytesRead += 64;
                        br.tailBytes = bytesRead & 3;
                        break;
                    }
                    bytesRead += len;
                }
            }
            catch (IOException e)
            {
                throw new BrotliRuntimeException("Failed to read input", e);
            }
            br.byteBuffer.SetLength(0);//clear
            br.byteBuffer.Write(br.shadowBuffer, 0, bytesRead & 0xFFFC);
            br.intBuffer.Position = 0;//rewind
            br.available = bytesRead >> 2;
        }

        public static void CheckHealth(BitReader br)
        {
            if (!br.endOfStreamReached)
            {
                return;
            }
            /* When end of stream is reached, we "borrow" up to 64 zeroes to bit reader.
             * If compressed stream is valid, then borrowed zeroes should remain unused. 
             */
            int valentBytes = (br.available << 2) + ((64 - br.bitOffset) >> 3);
            int borrowedBytes = 64 - br.tailBytes;
            if (valentBytes != borrowedBytes)
            {
                throw new BrotliRuntimeException("Read after end");
            }
        }

        /// <summary>
        /// Advances the Read buffer by 5 bytes to make room for reading next 24 bits.
        /// </summary>
        public static void FillBitWindow(BitReader br)
        {
            if (br.bitOffset >= 32)
            {
                int temp = br.intBuffer.ReadInt32();
                br.accumulator = ((long)temp << 32) | (long)((ulong)br.accumulator >> 32);
                br.bitOffset -= 32;
                br.available--;
            }
        }

        /// <summary>
        /// Reads the specified number of bits from Read Buffer.
        /// </summary>
        public static int ReadBits(BitReader br, int n)
        {
            FillBitWindow(br);
            int val = (int)((ulong)br.accumulator >> br.bitOffset) & ((1 << n) - 1);
            br.bitOffset += n;
            return val;
        }

        /// <summary>
        /// Initialize bit reader.
        /// <para>Initialisation turns bit reader to a ready state. Also a number of bytes is prefetched to</para>
        /// <para>accumulator. Because of that this method may block until enough data could be read from input.</para>
        /// </summary>
        /// <param name="br">BitReader POJO</param>
        /// <param name="input">data source</param>
        public static void Init(BitReader br, Stream input)
        {
            if (br.input != null)
            {
                throw new InvalidOperationException("Bit reader already has associated input stream");
            }
            br.input = input;
            br.accumulator = 0;
            br.intBuffer.Position = (READ_SIZE >> 2);
            br.bitOffset = 64;
            br.available = 0;
            br.endOfStreamReached = false;
            ReadMoreInput(br);
            /* This situation is impossible in current implementation. */
            if (br.available == 0)
            {
                throw new BrotliRuntimeException("Can't initialize reader");
            }
            FillBitWindow(br);
            FillBitWindow(br);
        }

        public static void Close(BitReader br)
        {
            try
            {
                Stream input = br.input;
                br.input = null;
                input = null;
            }
            catch (IOException ex)
            {
                throw ex;
            }
        }

        public static void JumpToByteBoundry(BitReader br)
        {
            int padding = (64 - br.bitOffset) & 7;
            if (padding != 0)
            {
                int paddingBits = BitReader.ReadBits(br, padding);
                if (paddingBits != 0)
                {
                    throw new BrotliRuntimeException("Corrupted padding bits ");
                }
            }
        }
    }
}
