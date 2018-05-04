namespace CSharpBrotli.Decode
{
    /// <summary>
    /// Contains a collection of huffman trees with the same alphabet size.
    /// </summary>
    public class HuffmanTreeGroup
    {
        /// <summary>
        /// The maximal alphabet size in this group.
        /// </summary>
        private int alphabetSize;

        /// <summary>
        /// Storage for Huffman lookup tables.
        /// </summary>
        //public int[] Codes { get { return _codes; } set { _codes = value; } }
        public int[] codes;

        /// <summary>
        /// Offsets of distinct lookup tables in <see cref="codes"/> storage.
        /// </summary>
        public int[] trees;

        /// <summary>
        /// Initializes the Huffman tree group.
        /// </summary>
        /// <param name="group">POJO to be initialised</param>
        /// <param name="alphabetSize">the maximal alphabet size in this group</param>
        /// <param name="n">number of Huffman codes</param>
        public static void Init(HuffmanTreeGroup group, int alphabetSize, int n)
        {
            group.alphabetSize = alphabetSize;
            group.codes = new int[n * Huffman.HUFFMAN_MAX_TABLE_SIZE];
            group.trees = new int[n];
        }

        /// <summary>
        /// Decodes Huffman trees from input stream and constructs lookup tables.
        /// </summary>
        /// <param name="group">target POJO</param>
        /// <param name="br">data source</param>
        public static void Decode(HuffmanTreeGroup group, BitReader br)
        {
            int next = 0;
            int n = group.trees.Length;
            for (int i = 0; i < n; i++)
            {
                group.trees[i] = next;
                CSharpBrotli.Decode.Decode.ReadHuffmanCode(group.alphabetSize, group.codes, next, br);
                next += Huffman.HUFFMAN_MAX_TABLE_SIZE;
            }
        }
    }
}