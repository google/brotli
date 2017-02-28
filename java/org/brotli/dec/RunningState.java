/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.dec;

/**
 * Enumeration of decoding state-machine.
 */
enum RunningState {
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
