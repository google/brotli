using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace CSharpBrotliTest
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();
        }

        private void button1_Click(object sender, EventArgs e)
        {
            string[] args = { "../../test_data.zip" };
            CheckBundle(args);
            Console.WriteLine("decode test_data.zip successfully.");
        }

        private void CheckBundle(string[] args)
        {
            int argsOffset = 0;
            bool sanityCheck = false;
            if (args.Length != 0)
            {
                if (args[0].Equals("-s"))
                {
                    sanityCheck = true;
                    argsOffset = 1;
                }
            }
            if (args.Length == argsOffset)
            {
                throw new Exception("Usage: BundleChecker [-s] <fileX.zip> ...");
            }
            for (int i = argsOffset; i < args.Length; i++)
            {
                byte[] data = File.ReadAllBytes(args[i]);
                MemoryStream input = new MemoryStream(data);
                new BundleChecker(input, 0, sanityCheck).Check();
            }
        }
    }
}
