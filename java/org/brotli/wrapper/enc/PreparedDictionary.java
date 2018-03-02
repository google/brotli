/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

package org.brotli.wrapper.enc;

import java.nio.ByteBuffer;

/**
 * BrotliEncoderPreparedDictionary holder.
 */
public class PreparedDictionary {

  final boolean isNative;
  ByteBuffer data;

  PreparedDictionary(ByteBuffer data, boolean isNative) {
    this.data = data;
    this.isNative = isNative;
  }

  @Override
  protected void finalize() throws Throwable {
    try {
      if (isNative) {
        ByteBuffer data = this.data;
        this.data = null;
        EncoderJNI.destroyDictionary(data);
      }
    } finally {
      super.finalize();
    }
  }
}
