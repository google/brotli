using System;
using System.IO;

namespace CSharpBrotli.Decode
{
    /// <summary>
    /// {@link InputStream} decorator that decompresses brotli data.
    /// <para>Not thread-safe.</para>
    /// </summary>
    public class BrotliInputStream
    {
        public const int DEFAULT_INTERNAL_BUFFER_SIZE = 16384;

        /// <summary>
        /// Internal buffer used for efficient byte-by-byte reading.
        /// </summary>
        private byte[] buffer;

        /// <summary>
        /// Number of decoded but still unused bytes in internal buffer.
        /// </summary>
        private int remainingBufferBytes;

        /// <summary>
        /// Next unused byte offset.
        /// </summary>
        private int bufferOffset;

        /// <summary>
        /// Decoder state.
        /// </summary>
        private readonly State state = new State();

        /// <summary>
        /// Creates a <see cref="Stream"/>  wrapper that decompresses brotli data.
        /// <para>For byte-by-byte reading (<see cref="Read"/>) internal buffer with
        /// <see cref="DEFAULT_INTERNAL_BUFFER_SIZE"/> size is allocated and used.
        /// </para>
        /// <para>Will block the thread until first kilobyte of data of source is available.</para>
        /// </summary>
        /// <param name="source">underlying data source</param>
        public BrotliInputStream(Stream source) : this(source, DEFAULT_INTERNAL_BUFFER_SIZE, null) { }

        /// <summary>
        /// Creates a <see cref="Stream"/> wrapper that decompresses brotli data.
        /// <para>For byte-by-byte reading (<see cref="Read"/> ) internal buffer of specified size is
        /// allocated and used.
        /// </para>
        /// <para>Will block the thread until first kilobyte of data of source is available.</para>
        /// </summary>
        /// <param name="source">compressed data source</param>
        /// <param name="byteReadBufferSize">size of internal buffer used in case of byte-by-byte reading</param>
        public BrotliInputStream(Stream source, int byteReadBufferSize) : this(source, byteReadBufferSize, null) { }

        /// <summary>
        /// Creates a <see cref="Stream"/> wrapper that decompresses brotli data.
        /// <para>For byte-by-byte reading (<see cref="Read"/> ) internal buffer of specified size is
        /// allocated and used.
        /// </para>
        /// <para>Will block the thread until first kilobyte of data of source is available.</para>
        /// </summary>
        /// <param name="source">compressed data source</param>
        /// <param name="byteReadBufferSize">size of internal buffer used in case of byte-by-byte reading</param>
        /// <param name="customDictionary">custom dictionary data; null if not used</param>
        public BrotliInputStream(Stream source, int byteReadBufferSize, byte[] customDictionary)
        {
            try
            {
                if (byteReadBufferSize <= 0)
                {
                    throw new InvalidOperationException("Bad buffer size:" + byteReadBufferSize);
                }
                else if (source == null)
                {
                    throw new InvalidOperationException("source is null");
                }
                this.buffer = new byte[byteReadBufferSize];
                this.remainingBufferBytes = 0;
                this.bufferOffset = 0;
                try
                {
                    State.SetInput(state, source);
                }
                catch (BrotliRuntimeException ex)
                {
                    throw new IOException(ex.Message, ex.InnerException);
                }
                if (customDictionary != null)
                {
                    Decode.SetCustomDictionary(state, customDictionary);
                }
            }
            catch(IOException ex)
            {
                throw ex;
            }
        }

        public void Close()
        {
            try
            {
                State.Close(state);
            }
            catch (IOException ex)
            {
                throw ex;
            }
        }

        public int Read()
        {
            try
            {
                if (bufferOffset >= remainingBufferBytes)
                {
                    remainingBufferBytes = Read(buffer, 0, buffer.Length);
                    bufferOffset = 0;
                    if (remainingBufferBytes == -1)
                    {
                        return -1;
                    }
                }
                return buffer[bufferOffset++] & 0xFF;
            }
            catch(IOException ex)
            {
                throw ex;
            }
            catch(BrotliRuntimeException ex)
            {
                throw new IOException(ex.Message, ex.InnerException);
            }
        }

        public int Read(byte[] destBuffer)
        {
            return Read(destBuffer, 0, destBuffer.Length);
        }

        public int Read(byte[] destBuffer, int destOffset, int destLen)
        {
            try
            {
                if (destOffset < 0)
                {
                    throw new InvalidOperationException("Bad offset: " + destOffset);
                }
                else if (destLen < 0)
                {
                    throw new InvalidOperationException("Bad length: " + destLen);
                }
                else if (destOffset + destLen > destBuffer.Length)
                {
                    throw new InvalidOperationException(
                        "Buffer overflow: " + (destOffset + destLen) + " > " + destBuffer.Length);
                }
                else if (destLen == 0)
                {
                    return 0;
                }
                int copyLen = Math.Max(remainingBufferBytes - bufferOffset, 0);
                if (copyLen != 0)
                {
                    copyLen = Math.Min(copyLen, destLen);
                    Array.Copy(buffer, bufferOffset, destBuffer, destOffset, copyLen);
                    bufferOffset += copyLen;
                    destOffset += copyLen;
                    destLen -= copyLen;
                    if (destLen == 0)
                    {
                        return copyLen;
                    }
                }
                try
                {
                    state.output = destBuffer;
                    state.outputOffset = destOffset;
                    state.outputLength = destLen;
                    state.outputUsed = 0;
                    Decode.Decompress(state);
                    if (state.outputUsed == 0)
                    {
                        return -1;
                    }
                    return state.outputUsed + copyLen;
                }
                catch (BrotliRuntimeException ex)
                {
                    throw new IOException(ex.Message, ex.InnerException);
                }
            }
            catch (IOException ex)
            {
                throw ex;
            }
        }
    }
}