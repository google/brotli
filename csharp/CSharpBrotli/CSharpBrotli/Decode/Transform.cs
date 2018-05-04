namespace CSharpBrotli.Decode
{
    /// <summary>
    /// Transformations on dictionary words.
    /// </summary>
    public sealed class Transform
    {
        private readonly byte[] prefix;
        private readonly WordTransformType type;
        private readonly byte[] suffix;

        public Transform(string prefix, WordTransformType type, string suffix)
        {
            this.prefix = ReadUniBytes(prefix);
            this.type = type;
            this.suffix = ReadUniBytes(suffix);
        }

        public static byte[] ReadUniBytes(string uniBytes)
        {
            byte[] result = new byte[uniBytes.Length];
            for(int i = 0; i < result.Length; i++)
            {
                result[i] = (byte)uniBytes[i];
            }
            return result;
        }

        public static readonly Transform[] TRANSFORMS =
        {
            new Transform("", IDENTITY, ""),
          new Transform("", IDENTITY, " "),
          new Transform(" ", IDENTITY, " "),
          new Transform("", OMIT_FIRST_1, ""),
          new Transform("", UPPERCASE_FIRST, " "),
          new Transform("", IDENTITY, " the "),
          new Transform(" ", IDENTITY, ""),
          new Transform("s ", IDENTITY, " "),
          new Transform("", IDENTITY, " of "),
          new Transform("", UPPERCASE_FIRST, ""),
          new Transform("", IDENTITY, " and "),
          new Transform("", OMIT_FIRST_2, ""),
          new Transform("", OMIT_LAST_1, ""),
          new Transform(", ", IDENTITY, " "),
          new Transform("", IDENTITY, ", "),
          new Transform(" ", UPPERCASE_FIRST, " "),
          new Transform("", IDENTITY, " in "),
          new Transform("", IDENTITY, " to "),
          new Transform("e ", IDENTITY, " "),
          new Transform("", IDENTITY, "\""),
          new Transform("", IDENTITY, "."),
          new Transform("", IDENTITY, "\">"),
          new Transform("", IDENTITY, "\n"),
          new Transform("", OMIT_LAST_3, ""),
          new Transform("", IDENTITY, "]"),
          new Transform("", IDENTITY, " for "),
          new Transform("", OMIT_FIRST_3, ""),
          new Transform("", OMIT_LAST_2, ""),
          new Transform("", IDENTITY, " a "),
          new Transform("", IDENTITY, " that "),
          new Transform(" ", UPPERCASE_FIRST, ""),
          new Transform("", IDENTITY, ". "),
          new Transform(".", IDENTITY, ""),
          new Transform(" ", IDENTITY, ", "),
          new Transform("", OMIT_FIRST_4, ""),
          new Transform("", IDENTITY, " with "),
          new Transform("", IDENTITY, "'"),
          new Transform("", IDENTITY, " from "),
          new Transform("", IDENTITY, " by "),
          new Transform("", OMIT_FIRST_5, ""),
          new Transform("", OMIT_FIRST_6, ""),
          new Transform(" the ", IDENTITY, ""),
          new Transform("", OMIT_LAST_4, ""),
          new Transform("", IDENTITY, ". The "),
          new Transform("", UPPERCASE_ALL, ""),
          new Transform("", IDENTITY, " on "),
          new Transform("", IDENTITY, " as "),
          new Transform("", IDENTITY, " is "),
          new Transform("", OMIT_LAST_7, ""),
          new Transform("", OMIT_LAST_1, "ing "),
          new Transform("", IDENTITY, "\n\t"),
          new Transform("", IDENTITY, ":"),
          new Transform(" ", IDENTITY, ". "),
          new Transform("", IDENTITY, "ed "),
          new Transform("", OMIT_FIRST_9, ""),
          new Transform("", OMIT_FIRST_7, ""),
          new Transform("", OMIT_LAST_6, ""),
          new Transform("", IDENTITY, "("),
          new Transform("", UPPERCASE_FIRST, ", "),
          new Transform("", OMIT_LAST_8, ""),
          new Transform("", IDENTITY, " at "),
          new Transform("", IDENTITY, "ly "),
          new Transform(" the ", IDENTITY, " of "),
          new Transform("", OMIT_LAST_5, ""),
          new Transform("", OMIT_LAST_9, ""),
          new Transform(" ", UPPERCASE_FIRST, ", "),
          new Transform("", UPPERCASE_FIRST, "\""),
          new Transform(".", IDENTITY, "("),
          new Transform("", UPPERCASE_ALL, " "),
          new Transform("", UPPERCASE_FIRST, "\">"),
          new Transform("", IDENTITY, "=\""),
          new Transform(" ", IDENTITY, "."),
          new Transform(".com/", IDENTITY, ""),
          new Transform(" the ", IDENTITY, " of the "),
          new Transform("", UPPERCASE_FIRST, "'"),
          new Transform("", IDENTITY, ". This "),
          new Transform("", IDENTITY, ","),
          new Transform(".", IDENTITY, " "),
          new Transform("", UPPERCASE_FIRST, "("),
          new Transform("", UPPERCASE_FIRST, "."),
          new Transform("", IDENTITY, " not "),
          new Transform(" ", IDENTITY, "=\""),
          new Transform("", IDENTITY, "er "),
          new Transform(" ", UPPERCASE_ALL, " "),
          new Transform("", IDENTITY, "al "),
          new Transform(" ", UPPERCASE_ALL, ""),
          new Transform("", IDENTITY, "='"),
          new Transform("", UPPERCASE_ALL, "\""),
          new Transform("", UPPERCASE_FIRST, ". "),
          new Transform(" ", IDENTITY, "("),
          new Transform("", IDENTITY, "ful "),
          new Transform(" ", UPPERCASE_FIRST, ". "),
          new Transform("", IDENTITY, "ive "),
          new Transform("", IDENTITY, "less "),
          new Transform("", UPPERCASE_ALL, "'"),
          new Transform("", IDENTITY, "est "),
          new Transform(" ", UPPERCASE_FIRST, "."),
          new Transform("", UPPERCASE_ALL, "\">"),
          new Transform(" ", IDENTITY, "='"),
          new Transform("", UPPERCASE_FIRST, ","),
          new Transform("", IDENTITY, "ize "),
          new Transform("", UPPERCASE_ALL, "."),
          new Transform("\u00c2\u00a0", IDENTITY, ""),
          new Transform(" ", IDENTITY, ","),
          new Transform("", UPPERCASE_FIRST, "=\""),
          new Transform("", UPPERCASE_ALL, "=\""),
          new Transform("", IDENTITY, "ous "),
          new Transform("", UPPERCASE_ALL, ", "),
          new Transform("", UPPERCASE_FIRST, "='"),
          new Transform(" ", UPPERCASE_FIRST, ","),
          new Transform(" ", UPPERCASE_ALL, "=\""),
          new Transform(" ", UPPERCASE_ALL, ", "),
          new Transform("", UPPERCASE_ALL, ","),
          new Transform("", UPPERCASE_ALL, "("),
          new Transform("", UPPERCASE_ALL, ". "),
          new Transform(" ", UPPERCASE_ALL, "."),
          new Transform("", UPPERCASE_ALL, "='"),
          new Transform(" ", UPPERCASE_ALL, ". "),
          new Transform(" ", UPPERCASE_FIRST, "=\""),
          new Transform(" ", UPPERCASE_ALL, "='"),
          new Transform(" ", UPPERCASE_FIRST, "='")
        };

        private const WordTransformType IDENTITY = WordTransformType.IDENTITY;
        private const WordTransformType OMIT_LAST_1 = WordTransformType.OMIT_LAST_1;
        private const WordTransformType OMIT_LAST_2 = WordTransformType.OMIT_LAST_2;
        private const WordTransformType OMIT_LAST_3 = WordTransformType.OMIT_LAST_3;
        private const WordTransformType OMIT_LAST_4 = WordTransformType.OMIT_LAST_4;
        private const WordTransformType OMIT_LAST_5 = WordTransformType.OMIT_LAST_5;
        private const WordTransformType OMIT_LAST_6 = WordTransformType.OMIT_LAST_6;
        private const WordTransformType OMIT_LAST_7 = WordTransformType.OMIT_LAST_7;
        private const WordTransformType OMIT_LAST_8 = WordTransformType.OMIT_LAST_8;
        private const WordTransformType OMIT_LAST_9 = WordTransformType.OMIT_LAST_9;
        private const WordTransformType UPPERCASE_FIRST = WordTransformType.UPPERCASE_FIRST;
        private const WordTransformType UPPERCASE_ALL = WordTransformType.UPPERCASE_ALL;
        private const WordTransformType OMIT_FIRST_1 = WordTransformType.OMIT_FIRST_1;
        private const WordTransformType OMIT_FIRST_2 = WordTransformType.OMIT_FIRST_2;
        private const WordTransformType OMIT_FIRST_3 = WordTransformType.OMIT_FIRST_3;
        private const WordTransformType OMIT_FIRST_4 = WordTransformType.OMIT_FIRST_4;
        private const WordTransformType OMIT_FIRST_5 = WordTransformType.OMIT_FIRST_5;
        private const WordTransformType OMIT_FIRST_6 = WordTransformType.OMIT_FIRST_6;
        private const WordTransformType OMIT_FIRST_7 = WordTransformType.OMIT_FIRST_7;
        private const WordTransformType OMIT_FIRST_8 = WordTransformType.OMIT_FIRST_8;
        private const WordTransformType OMIT_FIRST_9 = WordTransformType.OMIT_FIRST_9;

        public static int TransformDictionaryWord(byte[] dest, int dstOffset, byte[] word, int wordOffset, 
            int len, Transform transform)
        {
            int offset = dstOffset;

            // Copy prefix.
            byte[] str = transform.prefix;
            int tmp = str.Length;
            int i = 0;
            // In most cases tmp < 10 -> no benefits from System.arrayCopy
            while (i < tmp)
            {
                dest[offset++] = str[i++];
            }

            // Copy trimmed word.
            WordTransformType op = transform.type;
            tmp = TransformType.GetOmitFirst(op);// op.omitFirst;
            if (tmp > len)
            {
                tmp = len;
            }
            wordOffset += tmp;
            len -= tmp;
            len -= TransformType.GetOmitLast(op);//op.omitLast;
            i = len;
            while (i > 0)
            {
                dest[offset++] = word[wordOffset++];
                i--;
            }

            if (op == UPPERCASE_ALL || op == UPPERCASE_FIRST)
            {
                int uppercaseOffset = offset - len;
                if (op == UPPERCASE_FIRST)
                {
                    len = 1;
                }
                while (len > 0)
                {
                    tmp = dest[uppercaseOffset] & 0xFF;
                    if (tmp < 0xc0)
                    {
                        if (tmp >= 'a' && tmp <= 'z')
                        {
                            dest[uppercaseOffset] ^= (byte)32;
                        }
                        uppercaseOffset += 1;
                        len -= 1;
                    }
                    else if (tmp < 0xe0)
                    {
                        dest[uppercaseOffset + 1] ^= (byte)32;
                        uppercaseOffset += 2;
                        len -= 2;
                    }
                    else
                    {
                        dest[uppercaseOffset + 2] ^= (byte)5;
                        uppercaseOffset += 3;
                        len -= 3;
                    }
                }
            }

            // Copy suffix.
            str = transform.suffix;
            tmp = str.Length;
            i = 0;
            while (i < tmp)
            {
                dest[offset++] = str[i++];
            }

            return offset - dstOffset;
        }
    }
}