namespace CSharpBrotli.Decode
{
    /// <summary>
    /// Enumeration of decoding state-machine.
    /// </summary>
    public enum RunningStage
    {
        UNINITIALIZED,
        BLOCK_START,
        COMPRESSED_BLOCK_START,
        MAIN_LOOP,
        READ_METADATA,
        COPY_UNCOMPRESSED,
        INSERT_LOOP,
        COPY_LOOP,
        COPY_WRAP_BUFFER,
        TRANSFORM,
        FINISHED,
        CLOSED,
        WRITE
    }
}