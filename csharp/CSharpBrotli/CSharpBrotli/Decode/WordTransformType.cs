namespace CSharpBrotli.Decode
{
    public enum WordTransformType
    {
        IDENTITY,
        OMIT_LAST_1,
        OMIT_LAST_2,
        OMIT_LAST_3,
        OMIT_LAST_4,
        OMIT_LAST_5,
        OMIT_LAST_6,
        OMIT_LAST_7,
        OMIT_LAST_8,
        OMIT_LAST_9,
        UPPERCASE_FIRST,
        UPPERCASE_ALL,
        OMIT_FIRST_1,
        OMIT_FIRST_2,
        OMIT_FIRST_3,
        OMIT_FIRST_4,
        OMIT_FIRST_5,
        OMIT_FIRST_6,
        OMIT_FIRST_7,
        /*
         * brotli specification doesn't use OMIT_FIRST_8(8, 0) transform.
         * Probably, it would be used in future format extensions.
         */
        OMIT_FIRST_8,
        OMIT_FIRST_9
    }

    internal class TransformType
    {
        public static int GetOmitFirst(WordTransformType type)
        {
            if(type>=WordTransformType.OMIT_FIRST_1 && type<= WordTransformType.OMIT_FIRST_9)
            {
                return (type - WordTransformType.OMIT_FIRST_1) + 1;
            }
            return 0;
        }

        public static int GetOmitLast(WordTransformType type)
        {
            if(type >= WordTransformType.OMIT_LAST_1 && type<= WordTransformType.OMIT_LAST_9)
            {
                return (type - WordTransformType.OMIT_LAST_1) + 1;
            }
            return 0;
        }
    }
}