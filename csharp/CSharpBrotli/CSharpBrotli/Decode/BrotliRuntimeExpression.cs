using System;

namespace CSharpBrotli.Decode
{
    /// <summary>
    /// Unchecked exception used internally.
    /// </summary>
    public class BrotliRuntimeException : Exception
    {
        public BrotliRuntimeException() : base() { }
        public BrotliRuntimeException(string message) : base(message) { }
        public BrotliRuntimeException(string message, Exception innerException) : base(message, innerException) { }
    }
}