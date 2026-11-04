/* Copyright 2026 Google Inc. All Rights Reserved.

Distributed under MIT license.
See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/
namespace Org.Brotli.Dec
{
	/// <summary>
	/// Tests that the decoder follows the .NET end-of-stream convention:
	/// <see cref="System.IO.Stream.Read(byte[], int, int)"/>
	/// returns 0 (not -1, as in Java) once the stream is exhausted.
	/// </summary>
	public class EndOfStreamTest
	{
		/// <summary>
		/// Input stream that fails the test if it is polled over and over after it has
		/// already reported the end of stream (i.e. the consumer is stuck in a loop).
		/// </summary>
		private sealed class EndOfStreamGuard : System.IO.MemoryStream
		{
			private const int MaxReadsAfterEnd = 16;

			private int readsAfterEnd;

			public EndOfStreamGuard(byte[] data)
				: base(data)
			{
			}

			public override int Read(byte[] buffer, int offset, int count)
			{
				int len = base.Read(buffer, offset, count);
				if (len == 0 && count > 0)
				{
					readsAfterEnd++;
					if (readsAfterEnd > MaxReadsAfterEnd)
					{
						NUnit.Framework.Assert.Fail("Input is read in a loop after the end of stream");
					}
				}
				return len;
			}
		}

		/// <summary>"XXXXXXXXXXYYYYYYYYYY" compressed.</summary>
		private static readonly byte[] X10Y10 = Org.Brotli.Dec.Transform.ReadUniBytes("\u001B\u0013\u0000\u0000\u00A4\u00B0\u00B2\u00EA\u0081G\u0002\u008A");

		private static readonly byte[] X10Y10Expected = Org.Brotli.Dec.Transform.ReadUniBytes("XXXXXXXXXXYYYYYYYYYY");

		/// <summary>
		/// Header of a non-last uncompressed meta-block with MLEN = 65536 (WBITS = 16),
		/// followed by only 100 bytes of payload.
		/// </summary>
		private static byte[] TruncatedUncompressedMetaBlock()
		{
			byte[] data = new byte[3 + 100];
			data[0] = unchecked((byte)0xF0);
			data[1] = unchecked((byte)0xFF);
			data[2] = unchecked((byte)0x1F);
			for (int i = 3; i < data.Length; ++i)
			{
				data[i] = (byte)'X';
			}
			return data;
		}

		[NUnit.Framework.Test]
		public virtual void TestTruncatedUncompressedMetaBlock()
		{
			Org.Brotli.Dec.BrotliInputStream brotliInput = new Org.Brotli.Dec.BrotliInputStream(new EndOfStreamGuard(TruncatedUncompressedMetaBlock()));
			byte[] buffer = new byte[65536];
			try
			{
				while (brotliInput.Read(buffer, 0, buffer.Length) > 0)
				{
				}
			}
			catch (System.IO.IOException)
			{
				// This exception is expected.
				return;
			}
			finally
			{
				brotliInput.Close();
			}
			NUnit.Framework.Assert.Fail("IOException should have been thrown for truncated input");
		}

		[NUnit.Framework.Test]
		public virtual void TestReadReturnsZeroAtEndOfStream()
		{
			Org.Brotli.Dec.BrotliInputStream brotliInput = new Org.Brotli.Dec.BrotliInputStream(new System.IO.MemoryStream(X10Y10));
			byte[] buffer = new byte[64];
			NUnit.Framework.Assert.AreEqual(X10Y10Expected.Length, brotliInput.Read(buffer, 0, buffer.Length));
			NUnit.Framework.Assert.AreEqual(0, brotliInput.Read(buffer, 0, buffer.Length));
			NUnit.Framework.Assert.AreEqual(-1, brotliInput.ReadByte());
			brotliInput.Close();
		}

		[NUnit.Framework.Test]
		public virtual void TestCopyTo()
		{
			Org.Brotli.Dec.BrotliInputStream brotliInput = new Org.Brotli.Dec.BrotliInputStream(new System.IO.MemoryStream(X10Y10));
			System.IO.MemoryStream output = new System.IO.MemoryStream();
			brotliInput.CopyTo(output);
			brotliInput.Close();
			NUnit.Framework.Assert.AreEqual(X10Y10Expected, output.ToArray());
		}

		[NUnit.Framework.Test]
		public virtual void TestReadByteThenRead()
		{
			Org.Brotli.Dec.BrotliInputStream brotliInput = new Org.Brotli.Dec.BrotliInputStream(new System.IO.MemoryStream(X10Y10));
			byte[] buffer = new byte[64];
			NUnit.Framework.Assert.AreEqual(X10Y10Expected[0], brotliInput.ReadByte());
			// The rest of the data is already buffered; it must not be dropped.
			int len = brotliInput.Read(buffer, 0, buffer.Length);
			NUnit.Framework.Assert.AreEqual(X10Y10Expected.Length - 1, len);
			for (int i = 0; i < len; ++i)
			{
				NUnit.Framework.Assert.AreEqual(X10Y10Expected[i + 1], buffer[i]);
			}
			NUnit.Framework.Assert.AreEqual(0, brotliInput.Read(buffer, 0, buffer.Length));
			brotliInput.Close();
		}
	}
}
