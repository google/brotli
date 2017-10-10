#ifndef BROTLI_RESEARCH_DEORUMMOLAE_H_
#define BROTLI_RESEARCH_DEORUMMOLAE_H_

#include <stddef.h>
#include <stdint.h>

/* log2(maximal number of files). Value 6 provides some speedups. */
#define LOG_MAX_FILES 6

/* Non tunable definitions. */
#define MAX_FILES (1 << LOG_MAX_FILES)

/**
 * Generate a dictionary for given samples.
 *
 * @param dictionary storage for generated dictionary
 * @param dictionary_size_limit maximal dictionary size
 * @param num_samples number of samples
 * @param sample_sizes array with sample sizes
 * @param sample_data concatenated samples
 * @return generated dictionary size
 */
size_t DM_generate(uint8_t* dictionary, size_t dictionary_size_limit,
    size_t num_samples, const size_t* sample_sizes,
    const uint8_t* sample_data);

#endif  // BROTLI_RESEARCH_DEORUMMOLAE_H_
