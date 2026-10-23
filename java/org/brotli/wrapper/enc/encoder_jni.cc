/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_set>

#include <brotli/encode.h>
#include <brotli/shared_dictionary.h>

namespace {
/* A structure used to persist the encoder's state in between calls. */
typedef struct EncoderHandle {
  BrotliEncoderState* state;

  jobject dictionary_refs[15];
  size_t dictionary_count;

  uint8_t* input_start;
  size_t input_offset;
  size_t input_last;
} EncoderHandle;

/* Obtain handle from opaque pointer. */
EncoderHandle* getHandle(void* opaque) {
  return static_cast<EncoderHandle*>(opaque);
}

/* Serialized prepared-dictionary blob header. Must match the
 * PreparedDictionary layout in c/enc/compound_dictionary.h. */
struct PreparedDictionaryHeader {
  uint32_t magic;
  uint32_t num_items;
  uint32_t source_size;
  uint32_t hash_bits;
  uint32_t bucket_bits;
  uint32_t slot_bits;
};

/* Magic values from c/enc/compound_dictionary.h. */
const uint32_t kManagedDictionaryMagic = 0xDEBCEDE2u;
const uint32_t kPreparedDictionaryMagic = 0xDEBCEDE0u;
const uint32_t kLeanPreparedDictionaryMagic = 0xDEBCEDE3u;

/* Bounds mirroring the encoder's own dictionary builder
 * (CreatePreparedDictionaryWithParams rejects slot_bits > 16 and
 * bucket_bits - slot_bits >= 16). */
const uint32_t kMaxDictionarySlotBits = 16;
const uint32_t kMaxDictionaryBucketBits = 24;

/* Returns true iff the [address, address + capacity) region holds a
 * well-formed prepared-dictionary blob: the header magic and fields are
 * sane, and every table AttachPreparedDictionary derives from them
 * (slot_offsets, heads, items, tail/source) lies inside the buffer.
 * All size arithmetic is overflow-checked. */
bool IsValidPreparedDictionaryBlob(const uint8_t* address, size_t capacity) {
  if (capacity < sizeof(PreparedDictionaryHeader)) return false;
  PreparedDictionaryHeader header;
  memcpy(&header, address, sizeof(header));
  if (header.magic != kPreparedDictionaryMagic &&
      header.magic != kLeanPreparedDictionaryMagic) {
    return false;
  }
  if (header.slot_bits > kMaxDictionarySlotBits) return false;
  if (header.bucket_bits > kMaxDictionaryBucketBits) return false;
  /* hash_bits feeds (64 - hash_bits) shifts; keep it in range. */
  if (header.hash_bits == 0 || header.hash_bits > 64) return false;
  size_t num_slots = (size_t)1u << header.slot_bits;
  size_t num_buckets = (size_t)1u << header.bucket_bits;
  size_t slot_table = num_slots * sizeof(uint32_t);
  size_t head_table = num_buckets * sizeof(uint16_t);
  size_t item_table = (size_t)header.num_items * sizeof(uint32_t);
  if (slot_table / sizeof(uint32_t) != num_slots) return false;
  if (head_table / sizeof(uint16_t) != num_buckets) return false;
  if (item_table / sizeof(uint32_t) != header.num_items) return false;
  size_t required = sizeof(PreparedDictionaryHeader);
  if (required > SIZE_MAX - slot_table) return false;
  required += slot_table;
  if (required > SIZE_MAX - head_table) return false;
  required += head_table;
  if (required > SIZE_MAX - item_table) return false;
  required += item_table;
  if (header.magic == kPreparedDictionaryMagic) {
    /* Full blob: source[source_size] follows the tables. */
    if (required > SIZE_MAX - header.source_size) return false;
    required += header.source_size;
  } else {
    /* Lean blob: an 8-byte source pointer follows the tables. */
    if (required > SIZE_MAX - sizeof(uint64_t)) return false;
    required += sizeof(uint64_t);
  }
  return required <= capacity;
}

/* Registry of managed dictionaries created by nativePrepareDictionary.
 * A kManagedDictionaryMagic blob is only honored when its address came from
 * our own prepare call: that keeps the legitimate prepare -> attach ->
 * destroy flow working while rejecting forged managed blobs, whose
 * header-embedded pointer would otherwise be dereferenced unchecked. */
std::mutex g_managed_dictionaries_mutex;
std::unordered_set<const void*> g_managed_dictionaries;

void RegisterManagedDictionary(const void* dictionary) {
  std::lock_guard<std::mutex> lock(g_managed_dictionaries_mutex);
  g_managed_dictionaries.insert(dictionary);
}

bool IsRegisteredManagedDictionary(const void* dictionary) {
  std::lock_guard<std::mutex> lock(g_managed_dictionaries_mutex);
  return g_managed_dictionaries.find(dictionary) != g_managed_dictionaries.end();
}

/* Atomically checks registration and removes the entry. Used by the destroy
 * path so a managed dictionary cannot be destroyed twice. */
bool ClaimManagedDictionary(const void* dictionary) {
  std::lock_guard<std::mutex> lock(g_managed_dictionaries_mutex);
  auto it = g_managed_dictionaries.find(dictionary);
  if (it == g_managed_dictionaries.end()) {
    return false;
  }
  g_managed_dictionaries.erase(it);
  return true;
}

/* Returns true iff the caller-supplied direct buffer may be passed to
 * BrotliEncoderAttachPreparedDictionary: the magic is known, managed
 * dictionaries are restricted to ones our own nativePrepareDictionary
 * created, and prepared/lean blobs are validated against the real buffer
 * capacity (their interior tables are derived from header fields). */
bool IsAttachableDictionaryBlob(const uint8_t* address, jlong capacity) {
  if (capacity < 4) return false;
  uint32_t magic;
  memcpy(&magic, address, sizeof(magic));
  if (magic == kManagedDictionaryMagic) {
    return IsRegisteredManagedDictionary(address);
  }
  if (magic == kPreparedDictionaryMagic ||
      magic == kLeanPreparedDictionaryMagic) {
    return IsValidPreparedDictionaryBlob(address, (size_t)capacity);
  }
  return false;
}

}  /* namespace */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Creates a new Encoder.
 *
 * Cookie to address created encoder is stored in out_cookie. In case of failure
 * cookie is 0.
 *
 * @param ctx {out_cookie, in_directBufferSize, in_quality, in_lgwin} tuple
 * @returns direct ByteBuffer if directBufferSize is not 0; otherwise null
 */
JNIEXPORT jobject JNICALL Java_org_brotli_wrapper_enc_EncoderJNI_nativeCreate(
    JNIEnv* env, jobject /*jobj*/, jlongArray ctx) {
  bool ok = true;
  EncoderHandle* handle = nullptr;
  jlong context[5];
  env->functions->GetLongArrayRegion(env, ctx, 0, 5, context);
  size_t input_size = context[1];
  context[0] = 0;
  handle = new (std::nothrow) EncoderHandle();
  ok = !!handle;

  if (ok) {
    for (int i = 0; i < 15; ++i) {
      handle->dictionary_refs[i] = nullptr;
    }
    handle->dictionary_count = 0;
    handle->input_offset = 0;
    handle->input_last = 0;
    handle->input_start = nullptr;

    if (input_size == 0) {
      ok = false;
    } else {
      handle->input_start = new (std::nothrow) uint8_t[input_size];
      ok = !!handle->input_start;
    }
  }

  if (ok) {
    handle->state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
    ok = !!handle->state;
  }

  if (ok) {
    int quality = context[2];
    if (quality >= 0) {
      BrotliEncoderSetParameter(handle->state, BROTLI_PARAM_QUALITY, quality);
    }
    int lgwin = context[3];
    if (lgwin >= 0) {
      BrotliEncoderSetParameter(handle->state, BROTLI_PARAM_LGWIN, lgwin);
    }
    int mode = context[4];
    if (mode >= 0) {
      BrotliEncoderSetParameter(handle->state, BROTLI_PARAM_MODE, mode);
    }
  }

  if (ok) {
    /* TODO(eustas): future versions (e.g. when 128-bit architecture comes)
                     might require thread-safe cookie<->handle mapping. */
    context[0] = reinterpret_cast<jlong>(handle);
  } else if (!!handle) {
    if (!!handle->input_start) delete[] handle->input_start;
    delete handle;
  }

  env->functions->SetLongArrayRegion(env, ctx, 0, 1, context);

  if (!ok) {
    return nullptr;
  }

  return env->functions->NewDirectByteBuffer(env, handle->input_start,
                                             input_size);
}

/**
 * Push data to encoder.
 *
 * @param ctx {in_cookie, in_operation_out_success, out_has_more_output,
 *             out_has_remaining_input} tuple
 * @param input_length number of bytes provided in input or direct input;
 *                     0 to process further previous input
 */
JNIEXPORT void JNICALL Java_org_brotli_wrapper_enc_EncoderJNI_nativePush(
    JNIEnv* env, jobject /*jobj*/, jlongArray ctx, jint input_length) {
  jlong context[5];
  env->functions->GetLongArrayRegion(env, ctx, 0, 5, context);
  EncoderHandle* handle = getHandle(reinterpret_cast<void*>(context[0]));
  int operation = context[1];
  context[1] = 0;  /* ERROR */
  env->functions->SetLongArrayRegion(env, ctx, 0, 5, context);

  BrotliEncoderOperation op;
  switch (operation) {
    case 0: op = BROTLI_OPERATION_PROCESS; break;
    case 1: op = BROTLI_OPERATION_FLUSH; break;
    case 2: op = BROTLI_OPERATION_FINISH; break;
    default: return;  /* ERROR */
  }

  if (input_length != 0) {
    /* Still have unconsumed data. Workflow is broken. */
    if (handle->input_offset < handle->input_last) {
      return;
    }
    handle->input_offset = 0;
    handle->input_last = input_length;
  }

  /* Actual compression. */
  const uint8_t* in = handle->input_start + handle->input_offset;
  size_t in_size = handle->input_last - handle->input_offset;
  size_t out_size = 0;
  BROTLI_BOOL status = BrotliEncoderCompressStream(
      handle->state, op, &in_size, &in, &out_size, nullptr, nullptr);
  handle->input_offset = handle->input_last - in_size;
  if (!!status) {
    context[1] = 1;
    context[2] = BrotliEncoderHasMoreOutput(handle->state) ? 1 : 0;
    context[3] = (handle->input_offset != handle->input_last) ? 1 : 0;
    context[4] = BrotliEncoderIsFinished(handle->state) ? 1 : 0;
  }
  env->functions->SetLongArrayRegion(env, ctx, 0, 5, context);
}

/**
 * Pull decompressed data from encoder.
 *
 * @param ctx {in_cookie, out_success, out_has_more_output,
 *             out_has_remaining_input} tuple
 * @returns direct ByteBuffer; all the produced data MUST be consumed before
 *          any further invocation; null in case of error
 */
JNIEXPORT jobject JNICALL Java_org_brotli_wrapper_enc_EncoderJNI_nativePull(
    JNIEnv* env, jobject /*jobj*/, jlongArray ctx) {
  jlong context[5];
  env->functions->GetLongArrayRegion(env, ctx, 0, 5, context);
  EncoderHandle* handle = getHandle(reinterpret_cast<void*>(context[0]));
  size_t data_length = 0;
  const uint8_t* data = BrotliEncoderTakeOutput(handle->state, &data_length);
  context[1] = 1;
  context[2] = BrotliEncoderHasMoreOutput(handle->state) ? 1 : 0;
  context[3] = (handle->input_offset != handle->input_last) ? 1 : 0;
  context[4] = BrotliEncoderIsFinished(handle->state) ? 1 : 0;
  env->functions->SetLongArrayRegion(env, ctx, 0, 5, context);
  return env->functions->NewDirectByteBuffer(env, const_cast<uint8_t*>(data),
                                             data_length);
}

/**
 * Releases all used resources.
 *
 * @param ctx {in_cookie} tuple
 */
JNIEXPORT void JNICALL Java_org_brotli_wrapper_enc_EncoderJNI_nativeDestroy(
    JNIEnv* env, jobject /*jobj*/, jlongArray ctx) {
  jlong context[2];
  env->functions->GetLongArrayRegion(env, ctx, 0, 2, context);
  EncoderHandle* handle = getHandle(reinterpret_cast<void*>(context[0]));
  BrotliEncoderDestroyInstance(handle->state);
  for (size_t i = 0; i < handle->dictionary_count; ++i) {
    env->functions->DeleteGlobalRef(env, handle->dictionary_refs[i]);
  }
  delete[] handle->input_start;
  delete handle;
}

JNIEXPORT jboolean JNICALL
Java_org_brotli_wrapper_enc_EncoderJNI_nativeAttachDictionary(
    JNIEnv* env, jobject /*jobj*/, jlongArray ctx, jobject dictionary) {
  jlong context[2];
  env->functions->GetLongArrayRegion(env, ctx, 0, 2, context);
  EncoderHandle* handle = getHandle(reinterpret_cast<void*>(context[0]));
  jobject ref = nullptr;
  uint8_t* address = nullptr;

  bool ok = true;
  if (ok && !dictionary) {
    ok = false;
  }
  if (ok && handle->dictionary_count >= 15) {
    ok = false;
  }
  if (ok) {
    address = static_cast<uint8_t*>(
        env->functions->GetDirectBufferAddress(env, dictionary));
    ok = !!address;
  }
  if (ok) {
    /* The buffer is caller-controlled: validate it before reinterpreting.
     * A forged kManagedDictionaryMagic blob would otherwise make
     * BrotliEncoderAttachPreparedDictionary dereference an arbitrary
     * header-embedded pointer, and forged prepared/lean blobs yield wild
     * interior table pointers (heap OOB read during encoding). */
    jlong capacity = env->functions->GetDirectBufferCapacity(env, dictionary);
    ok = IsAttachableDictionaryBlob(address, capacity);
  }
  if (ok) {
    ref = env->functions->NewGlobalRef(env, dictionary);
    ok = !!ref;
  }
  if (ok) {
    handle->dictionary_refs[handle->dictionary_count] = ref;
    handle->dictionary_count++;
    ok = !!BrotliEncoderAttachPreparedDictionary(
        handle->state,
        reinterpret_cast<BrotliEncoderPreparedDictionary*>(address));
  }

  return static_cast<jboolean>(ok);
}

JNIEXPORT void JNICALL
Java_org_brotli_wrapper_enc_EncoderJNI_nativeDestroyDictionary(
    JNIEnv* env, jobject /*jobj*/, jobject dictionary) {
  if (!dictionary) {
    return;
  }
  uint8_t* address = static_cast<uint8_t*>(
      env->functions->GetDirectBufferAddress(env, dictionary));
  if (!address) {
    return;
  }
  jlong capacity = env->functions->GetDirectBufferCapacity(env, dictionary);
  if (capacity < 4) {
    return;
  }
  uint32_t magic;
  memcpy(&magic, address, sizeof(magic));
  /* Only managed dictionaries are eligible for destruction, and only ones
   * our own nativePrepareDictionary created: otherwise this would
   * dereference a forged header-embedded pointer. Claiming also prevents
   * double-destroy. */
  if (magic != kManagedDictionaryMagic || !ClaimManagedDictionary(address)) {
    return;
  }
  BrotliEncoderDestroyPreparedDictionary(
      reinterpret_cast<BrotliEncoderPreparedDictionary*>(address));
}

JNIEXPORT jobject JNICALL
Java_org_brotli_wrapper_enc_EncoderJNI_nativePrepareDictionary(
    JNIEnv* env, jobject /*jobj*/, jobject dictionary, jlong type) {
  if (!dictionary) {
    return nullptr;
  }
  uint8_t* address = static_cast<uint8_t*>(
      env->functions->GetDirectBufferAddress(env, dictionary));
  if (!address) {
    return nullptr;
  }
  jlong capacity = env->functions->GetDirectBufferCapacity(env, dictionary);
  if ((capacity <= 0) || (capacity >= (1 << 30))) {
    return nullptr;
  }
  BrotliSharedDictionaryType dictionary_type =
      static_cast<BrotliSharedDictionaryType>(type);
  size_t size = static_cast<size_t>(capacity);
  BrotliEncoderPreparedDictionary* prepared_dictionary =
      BrotliEncoderPrepareDictionary(dictionary_type, size, address,
                                     BROTLI_MAX_QUALITY, nullptr, nullptr,
                                     nullptr);
  if (!prepared_dictionary) {
    return nullptr;
  }
  /* Remember it: attach/destroy only honor managed dictionaries that came
   * from here (see IsAttachableDictionaryBlob). */
  RegisterManagedDictionary(prepared_dictionary);
  /* Size is 4 - just enough to check magic bytes. */
  return env->functions->NewDirectByteBuffer(env, prepared_dictionary, 4);
}

#ifdef __cplusplus
}
#endif
