using System;

namespace CSharpBrotli.Decode
{
    public sealed class Utils
    {
        private static readonly byte[] BYTE_ZEROES = new byte[1024];
        private static readonly int[] INT_ZEROES = new int[1024];

        /// <summary>
        /// Fills byte array with zeroes.
        /// <para>Current implementation uses <see cref="Array.Copy(Array, int, Array, int, int)"/>, so it should be used for 
        /// length not less than 16.</para>
        /// </summary>
        /// <param name="dest">array to fill with zeroes</param>
        /// <param name="offset">the first byte to fill</param>
        /// <param name="length">length number of bytes to change</param>
        public static void FillWithZeroes(byte[] dest, int offset, int length)
        {
            int cursor = 0;
            while (cursor < length)
            {
                int step = Math.Min(cursor + 1024, length) - cursor;
                Array.Copy(BYTE_ZEROES, 0, dest, offset + cursor, step);
                cursor += step;
            }
        }

        /// <summary>
        /// Fills int array with zeroes.
        /// <para>Current implementation uses <see cref="Array.Copy(Array, int, Array, int, int)"/>, so it should be used for length not 
        /// less than 16.</para>
        /// </summary>
        /// <param name="dest">array to fill with zeroes</param>
        /// <param name="offset">the first item to fill</param>
        /// <param name="length">number of item to change</param>
        public static void FillWithZeroes(int[] dest, int offset, int length)
        {
            int cursor = 0;
            while (cursor < length)
            {
                int step = Math.Min(cursor + 1024, length) - cursor;
                Array.Copy(INT_ZEROES, 0, dest, offset + cursor, step);
                cursor += step;
            }
        }
    }
}