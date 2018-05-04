using System;
using System.IO;
using ICSharpCode.SharpZipLib.Zip;
using CSharpBrotli.Decode;

namespace CSharpBrotliTest
{
    /// <summary>
    /// Decompress files and (optionally) checks their checksums.
    /// <para>File are read from ZIP archive passed as an array of bytes. Multiple checkers negotiate about
    /// task distribution via shared AtomicInteger counter.</para>
    /// <para>All entries are expected to be valid brotli compressed streams and output CRC64 checksum
    /// is expected to match the checksum hex-encoded in the first part of entry name.</para>
    /// </summary>
    public class BundleChecker
    {
        private int nextJob;
        private readonly Stream input;
        private bool sanityCheck;

        /// <param name="sanityCheck">do not calculate checksum and ignore <see cref="IOException"/></param>
        public BundleChecker(Stream input, int nextJob, bool sanityCheck)
        {
            this.input = input;
            this.nextJob = nextJob;
            this.sanityCheck = sanityCheck;
        }
        /// <summary>
        /// ECMA CRC64 polynomial.
        /// </summary>
        private readonly long CRC_64_POLY = Convert.ToInt64("0xC96C5795D7870F42", 16);

        private long UpdateCrc64(long crc, byte[] data, int offset, int length)
        {
            for (int i = offset; i < offset + length; ++i)
            {
                long c = (crc ^ (long)(data[i] & 0xFF)) & 0xFF;
                for (int k = 0; k < 8; k++)
                {
                    c = ((c & 1) == 1) ? CRC_64_POLY ^ (long)((ulong)c >> 1) : (long)((ulong)c >> 1);
                }
                crc = c ^ (long)((ulong)crc >> 8);
            }
            return crc;
        }

        private long DecompressAndCalculateCrc(Stream input)
        {
            try
            {
                long crc = -1;
                byte[] buffer = new byte[65536];
                BrotliInputStream decompressedStream = new BrotliInputStream(input);
                while (true)
                {
                    int len = decompressedStream.Read(buffer);
                    if (len <= 0)
                    {
                        break;
                    }
                    crc = UpdateCrc64(crc, buffer, 0, len);
                }
                decompressedStream.Close();
                return crc ^ -1;
            }
            catch (IOException ex)
            {
                throw ex;
            }
        }

        public void Check()
        {
            string entryName = "";
            ZipInputStream zipStream = new ZipInputStream(input);
            try
            {
                int entryIndex = 0;
                ZipEntry entry = null;
                int jobIndex = nextJob++;
                while ((entry = zipStream.GetNextEntry()) != null)
                {
                    if (entry.IsDirectory)
                    {
                        continue;
                    }
                    if (entryIndex++ != jobIndex)
                    {
                        zipStream.CloseEntry();
                        continue;
                    }
                    entryName = entry.Name;
                    int dotIndex = entryName.IndexOf('.');
                    string entryCrcString = (dotIndex == -1) ? entryName : entryName.Substring(0, dotIndex);
                    long entryCrc = Convert.ToInt64(entryCrcString, 16);
                    try
                    {
                        if (entryCrc != DecompressAndCalculateCrc(zipStream) && !sanityCheck)
                        {
                            throw new Exception("CRC mismatch");
                        }
                    }
                    catch (IOException iox)
                    {
                        if (!sanityCheck)
                        {
                            throw new Exception("Decompression failed", iox);
                        }
                    }
                    zipStream.CloseEntry();
                    entryName = "";
                    jobIndex = nextJob++;
                }
                zipStream.Close();
                input.Close();
            }
            catch (Exception ex)
            {
                throw ex;
            }
        }
    }
}