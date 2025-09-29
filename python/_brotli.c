#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PY_SSIZE_T_CLEAN 1
#include <Python.h>
#include <bytesobject.h>
#include <structmember.h>

#include <brotli/types.h>
#include <brotli/decode.h>
#include <brotli/encode.h>

#if PY_MAJOR_VERSION >= 3
#define PY_GET_TYPE(Obj) (Py_TYPE(Obj))
#else
#define PY_GET_TYPE(Obj) ((Obj)->ob_type)
#endif

static const char kErrorAttr[] = "error";
#if PY_MAJOR_VERSION >= 3
static const char kModuleAttr[] = "_module";
#else
static PyObject* BrotliError;
#endif

static const char kInvalidBufferError[] =
    "brotli: data must be a C-contiguous buffer";
static const char kOomError[] = "brotli: unable to allocate memory";
static const char kCompressCreateError[] =
    "brotli: failed to create native encoder instance";
static const char kCompressUnhealthyError[] = "brotli: encoder is unhealthy";
static const char kCompressConcurrentError[] =
    "brotli: encoder concurrent access";
static const char kCompressError[] = "brotli: encoder failed";
static const char kInvalidModeError[] = "brotli: invalid mode";
static const char kInvalidQualityError[] =
    "brotli: invalid quality; range is 0 to 11";
static const char kInvalidLgwinError[] =
    "brotli: invalid lgwin; range is 10 to 24";
static const char kInvalidLgblockError[] =
    "brotli: invalid lgblock; range is 16 to 24, or 0";
static const char kDecompressCreateError[] =
    "brotli: failed to create native decoder instance";
static const char kDecompressUnhealthyError[] = "brotli: decoder is unhealthy";
static const char kDecompressConcurrentError[] =
    "brotli: decoder concurrent access";
static const char kDecompressSinkError[] =
    "brotli: decoder process called with data when 'can_accept_more_data()' is "
    "False";
static const char kDecompressError[] = "brotli: decoder failed";

/* clang-format off */
PyDoc_STRVAR(brotli_error_doc,
"An error occurred in the Brotli implementation.");
PyDoc_STRVAR(brotli_Compressor_doc,
"An object to compress a byte string.\n"
"\n"
"Signature:\n"
"  Compressor(mode=MODE_GENERIC, quality=11, lgwin=22, lgblock=0)\n"
"\n"
"Args:\n"
"  mode (int): The compression mode can be MODE_GENERIC (default),\n"
"    MODE_TEXT (for UTF-8 format text input) or MODE_FONT (for WOFF 2.0).\n"
"  quality (int): Controls the compression-speed vs compression-\n"
"    density tradeoff. The higher the quality, the slower the compression.\n"
"    Must be in [0 .. 11]. Defaults to 11.\n"
"  lgwin (int): Base 2 logarithm of the sliding window size.\n"
"    Must be in [10 .. 24]. Defaults to 22.\n"
"  lgblock (int): Base 2 logarithm of the maximum input block size.\n"
"    Must be 0 or in [16 .. 24]. If set to 0, the value will be set based\n"
"    on the quality. Defaults to 0.\n"
"\n"
"Raises:\n"
"  brotli.error: If arguments are invalid.\n");
PyDoc_STRVAR(brotli_Compressor_process_doc,
"Process 'data' for compression, returning a bytes-object that contains\n"
"compressed output data. This data should be concatenated to the output\n"
"produced by any preceding calls to the 'process()' or 'flush()' methods.\n"
"Some or all of the input may be kept in internal buffers for later\n"
"processing, and the compressed output data may be empty until enough input\n"
"has been accumulated.\n"
"\n"
"Signature:\n"
"  process(data) -> bytes\n"
"\n"
"Args:\n"
"  data (bytes): The input data;\n"
"                MUST provide C-contiguous one-dimensional bytes view.\n"
"\n"
"Returns:\n"
"  The compressed output data (bytes)\n"
"\n"
"Raises:\n"
"  brotli.error: If compression fails\n");
PyDoc_STRVAR(brotli_Compressor_flush_doc,
"Process all pending input, returning a bytes-object containing the remaining\n"
"compressed data. This data should be concatenated to the output produced by\n"
"any preceding calls to the 'process()' or 'flush()' methods.\n"
"\n"
"Signature:\n"
"  flush() -> bytes\n"
"\n"
"Returns:\n"
"  The compressed output data (bytes)\n"
"\n"
"Raises:\n"
"  brotli.error: If compression fails\n");
PyDoc_STRVAR(brotli_Compressor_finish_doc,
"Process all pending input and complete all compression, returning\n"
"a 'bytes'-object containing the remaining compressed data. This data\n"
"should be concatenated to the output produced by any preceding calls\n"
"to the 'process()' or 'flush()' methods.\n"
"After calling 'finish()', the 'process()' and 'flush()' methods\n"
"cannot be called again, and a new \"Compressor\" object should be created.\n"
"\n"
"Signature:\n"
"  finish() -> bytes\n"
"\n"
"Returns:\n"
"  The compressed output data (bytes)\n"
"\n"
"Raises:\n"
"  brotli.error: If compression fails\n");
PyDoc_STRVAR(brotli_Decompressor_doc,
"An object to decompress a byte string.\n"
"\n"
"Signature:\n"
"  Decompressor()\n"
"\n"
"Raises:\n"
"  brotli.error: If arguments are invalid.\n");
PyDoc_STRVAR(brotli_Decompressor_process_doc,
"Process 'data' for decompression, returning a bytes-object that contains \n"
"decompressed output data. This data should be concatenated to the output\n"
"produced by any preceding calls to the 'process()' method.\n"
"Some or all of the input may be kept in internal buffers for later\n"
"processing, and the decompressed output data may be empty until enough input\n"
"has been accumulated.\n"
"\n"
"Signature:\n"
"  process(data, output_buffer_limit=int) -> bytes\n"
"\n"
"Args:\n"
"  data (bytes): The input data;\n"
"                MUST provide C-contiguous one-dimensional bytes view.\n"
"  output_buffer_limit (int): The maximum size of the output buffer.\n"
"                If set, the output buffer will not grow once its size\n"
"                equal or exceeding that value. If the limit is reached,\n"
"                further calls to process (potentially with empty input) will\n"
"                continue to yield more data. Following 'process()' must only\n"
"                be called with empty input until 'can_accept_more_data()'\n"
"                once returns True.\n"
"\n"
"Returns:\n"
"  The decompressed output data (bytes)\n"
"\n"
"Raises:\n"
"  brotli.error: If decompression fails\n");
PyDoc_STRVAR(brotli_Decompressor_is_finished_doc,
"Checks if decoder instance reached the final state.\n"
"\n"
"Signature:\n"
"  is_finished() -> bool\n"
"\n"
"Returns:\n"
"  True  if the decoder is in a state where it reached the end of the input\n"
"        and produced all of the output\n"
"  False otherwise\n");
PyDoc_STRVAR(brotli_Decompressor_can_accept_more_data_doc,
"Checks if the decoder instance can accept more compressed data.\n"
"If the 'decompress()' method on this instance of decompressor was never\n"
"called with 'max_length', this method will always return True.\n"
"\n"
"Signature:"
"  can_accept_more_data() -> bool\n"
"\n"
"Returns:\n"
"  True  if the decoder is ready to accept more compressed data via\n"
"        'decompress()'\n"
"  False if the decoder needs to output some data via 'decompress(b'')'\n"
"        before being provided any more compressed data\n");
PyDoc_STRVAR(brotli_decompress__doc__,
"Decompress a compressed byte string.\n"
"\n"
"Signature:\n"
"  decompress(data) -> bytes\n"
"\n"
"Args:\n"
"  data (bytes): The input data;\n"
"                MUST provide C-contiguous one-dimensional bytes view.\n"
"\n"
"Returns:\n"
"  The decompressed byte string.\n"
"\n"
"Raises:\n"
"  brotli.error: If decompressor fails.\n");
PyDoc_STRVAR(brotli_doc,
"Implementation module for the Brotli library.");
/* clang-format on */

static void set_brotli_exception(PyObject* t, const char* msg) {
#if PY_MAJOR_VERSION >= 3
  PyObject* error = NULL;
  PyObject* module = NULL;
  assert(t != NULL);
  assert(PyType_Check(t));
  module = PyObject_GetAttrString(t, kModuleAttr);
  if (!module) return; /* AttributeError raised. */
  error = PyObject_GetAttrString(module, kErrorAttr);
  Py_DECREF(module);
  if (error == NULL) return; /* AttributeError raised. */
  PyErr_SetString(error, msg);
  Py_DECREF(error);
#else
  PyErr_SetString(BrotliError, msg);
#endif
}

static void set_brotli_exception_from_module(PyObject* m, const char* msg) {
#if PY_MAJOR_VERSION >= 3
  PyObject* error = NULL;
  assert(m != NULL);
  assert(PyModule_Check(m));
  error = PyObject_GetAttrString(m, kErrorAttr);
  if (error == NULL) return; /* AttributeError raised. */
  PyErr_SetString(error, msg);
  Py_DECREF(error);
#else
  PyErr_SetString(BrotliError, msg);
#endif
}

/*
   Checks if the `object` provides a C-bytes buffer view.
   Returns non-zero on success.
   Returns zero and sets an exception on failure.
 */
static int get_data_view(PyObject* object, Py_buffer* view) {
  /* PyBUF_SIMPLE means: no shape, no strides, no sub-offsets. */
  if (PyObject_GetBuffer(object, view, PyBUF_SIMPLE) != 0) goto error;
  if (view->len == 0) return 1;
  /* https://docs.python.org/3/c-api/buffer.html:
     If shape is NULL as a result of a PyBUF_SIMPLE or a PyBUF_WRITABLE request,
    the consumer must disregard itemsize and assume itemsize == 1.*/
  if (((view->ndim == 0) || (view->ndim == 1)) && (view->shape == NULL) &&
      (view->strides == NULL)) {
    return 1;
  } else {
    PyBuffer_Release(view);
  }
error:
  PyErr_SetString(PyExc_TypeError, kInvalidBufferError);
  return 0;
}

/* --- Buffer --- */

typedef struct {
  size_t size;
  void* next; /* Block* */
  uint8_t payload[16];
} Block;

typedef struct {
  Block* head;
  Block* tail;
  size_t avail_out;
  uint8_t* next_out;
  size_t num_blocks;
  uint64_t total_allocated;
} Buffer;

static void Buffer_Init(Buffer* buffer) {
  buffer->head = NULL;
  buffer->tail = NULL;
  buffer->avail_out = 0;
  buffer->next_out = NULL;
  buffer->num_blocks = 0;
  buffer->total_allocated = 0;
}

/*
   Grow the buffer.

   Return 0 on success.
   Return -1 on failure, but do NOT raise an exception.
*/
static int Buffer_Grow(Buffer* buffer) {
  size_t log_size = buffer->num_blocks + 15;
  size_t size = 1 << (log_size > 24 ? 24 : log_size);
  size_t payload_size = size - offsetof(Block, payload);
  Block* block = NULL;

  assert(buffer->avail_out == 0);

  if (buffer->total_allocated >= PY_SSIZE_T_MAX - payload_size) return -1;

  assert(size > sizeof(Block));
  block = (Block*)malloc(size);
  if (block == NULL) return -1;
  block->size = payload_size;
  block->next = NULL;
  if (buffer->head == NULL) {
    buffer->head = block;
  } else {
    buffer->tail->next = block;
  }
  buffer->tail = block;
  buffer->next_out = block->payload;
  buffer->avail_out = payload_size;
  buffer->total_allocated += payload_size;
  buffer->num_blocks++;
  return 0;
}

static void Buffer_Cleanup(Buffer* buffer) {
  Block* block = NULL;
  if (buffer->head == NULL) return;
  block = buffer->head;
  buffer->head = NULL;
  while (block != NULL) {
    Block* next = block->next;
    block->next = NULL;
    free(block);
    block = next;
  }
}

/*
   Finish the buffer.

   Return a bytes object on success.
   Return NULL on OOM, but do NOT raise an exception.
*/
static PyObject* Buffer_Finish(Buffer* buffer) {
  uint64_t out_size = buffer->total_allocated - buffer->avail_out;
  Py_ssize_t len = (Py_ssize_t)out_size;
  PyObject* result = NULL;
  uint8_t* out = NULL;
  Py_ssize_t pos = 0;
  Block* block = NULL;
  size_t tail_len = 0;

  if ((uint64_t)len != out_size) {
    return NULL;
  }

  result = PyBytes_FromStringAndSize(NULL, len);
  if (result == NULL) {
    PyErr_Clear(); /* OOM exception will be raised by callers. */
    return NULL;
  }
  if (len == 0) return result;

  out = PyBytes_AS_STRING(result);
  block = buffer->head;
  while (block != buffer->tail) {
    memcpy(out + pos, block->payload, block->size);
    pos += block->size;
    block = block->next;
  }
  tail_len = block->size - buffer->avail_out;
  if (tail_len > 0) {
    memcpy(out + pos, block->payload, tail_len);
  }
  return result;
}

/* --- Compressor --- */

typedef struct {
  PyObject_HEAD BrotliEncoderState* enc;
  int healthy;
  int processing;
} PyBrotli_Compressor;

static PyObject* brotli_Compressor_new(PyTypeObject* type, PyObject* args,
                                       PyObject* keywds) {
  /* `tp_itemsize` is 0, so `nitems` could be 0. */
  PyBrotli_Compressor* self = (PyBrotli_Compressor*)type->tp_alloc(type, 0);
  PyObject* self_type = (PyObject*)type;

  if (self == NULL) return NULL;

  self->healthy = 0;
  self->processing = 0;
  self->enc = BrotliEncoderCreateInstance(0, 0, 0);
  if (self->enc == NULL) {
    set_brotli_exception(self_type, kCompressCreateError);
    PY_GET_TYPE(self)->tp_free((PyObject*)self);
    return NULL;
  }
  self->healthy = 1;

  return (PyObject*)self;
}

static int brotli_Compressor_init(PyBrotli_Compressor* self, PyObject* args,
                                  PyObject* keywds) {
  static const char* kwlist[] = {"mode", "quality", "lgwin", "lgblock", NULL};

  PyObject* self_type = (PyObject*)PY_GET_TYPE((PyObject*)self);
  unsigned char mode = BROTLI_DEFAULT_MODE;
  unsigned char quality = BROTLI_DEFAULT_QUALITY;
  unsigned char lgwin = BROTLI_DEFAULT_WINDOW;
  unsigned char lgblock = 0;

  int ok = PyArg_ParseTupleAndKeywords(args, keywds, "|bbbb:Compressor",
                                       (char**)kwlist, &mode, &quality, &lgwin,
                                       &lgblock);

  assert(self->healthy);

  if (!ok) {
    self->healthy = 0;
    return -1;
  }

  if ((mode == 0) || (mode == 1) || (mode == 2)) {
    BrotliEncoderSetParameter(self->enc, BROTLI_PARAM_MODE, (uint32_t)mode);
  } else {
    set_brotli_exception(self_type, kInvalidModeError);
    self->healthy = 0;
    return -1;
  }
  if (quality <= 11) {
    BrotliEncoderSetParameter(self->enc, BROTLI_PARAM_QUALITY,
                              (uint32_t)quality);
  } else {
    set_brotli_exception(self_type, kInvalidQualityError);
    self->healthy = 0;
    return -1;
  }
  if ((10 <= lgwin) && (lgwin <= 24)) {
    BrotliEncoderSetParameter(self->enc, BROTLI_PARAM_LGWIN, (uint32_t)lgwin);
  } else {
    set_brotli_exception(self_type, kInvalidLgwinError);
    self->healthy = 0;
    return -1;
  }
  if ((lgblock == 0) || ((16 <= lgblock) && (lgblock <= 24))) {
    BrotliEncoderSetParameter(self->enc, BROTLI_PARAM_LGBLOCK,
                              (uint32_t)lgblock);
  } else {
    set_brotli_exception(self_type, kInvalidLgblockError);
    self->healthy = 0;
    return -1;
  }

  return 0;
}

static void brotli_Compressor_dealloc(PyBrotli_Compressor* self) {
  if (self->enc) BrotliEncoderDestroyInstance(self->enc);
  PY_GET_TYPE(self)->tp_free((PyObject*)self);
}

/*
   Compress "utility knife" used for process / flush / finish.

   Continues processing until all input is consumed.
   Might return NULL on OOM or internal encoder error.
 */
static PyObject* compress_stream(PyBrotli_Compressor* self,
                                 BrotliEncoderOperation op, uint8_t* input,
                                 size_t input_length) {
  PyObject* self_type = (PyObject*)PY_GET_TYPE((PyObject*)self);
  size_t available_in = input_length;
  const uint8_t* next_in = input;
  Buffer buffer;
  PyObject* ret = NULL;
  int oom = 0;
  BROTLI_BOOL ok = BROTLI_FALSE;
  BrotliEncoderState* enc = self->enc;
  /* Callers must ensure mutually exclusive access to the encoder. */
  assert(self->processing == 1);

  Buffer_Init(&buffer);
  if (Buffer_Grow(&buffer) < 0) {
    oom = 1;
    goto error;
  }

  Py_BEGIN_ALLOW_THREADS;
  while (1) {
    ok = BrotliEncoderCompressStream(enc, op, &available_in, &next_in,
                                     &buffer.avail_out, &buffer.next_out, NULL);
    if (!ok) break;

    if ((available_in > 0) || BrotliEncoderHasMoreOutput(enc)) {
      if (buffer.avail_out == 0) {
        if (Buffer_Grow(&buffer) < 0) {
          oom = 1;
          break;
        }
      }
      continue;
    }
    break;
  }
  Py_END_ALLOW_THREADS;

  if (oom) goto error;
  if (ok) {
    ret = Buffer_Finish(&buffer);
    if (ret == NULL) oom = 1;
  } else { /* Not ok */
    set_brotli_exception(self_type, kCompressError);
  }

error:
  if (oom) {
    PyErr_SetString(PyExc_MemoryError, kOomError);
    assert(ret == NULL);
  }
  Buffer_Cleanup(&buffer);
  if (PyErr_Occurred() != NULL) {
    assert(ret == NULL);
    self->healthy = 0;
    return NULL;
  }
  return ret;
}

static PyObject* brotli_Compressor_process(PyBrotli_Compressor* self,
                                           PyObject* args) {
  PyObject* self_type = (PyObject*)PY_GET_TYPE((PyObject*)self);
  PyObject* ret = NULL;
  PyObject* input_object = NULL;
  Py_buffer input;

  if (self->healthy == 0) {
    set_brotli_exception(self_type, kCompressUnhealthyError);
    return NULL;
  }
  if (self->processing != 0) {
    set_brotli_exception(self_type, kCompressConcurrentError);
    return NULL;
  }

  if (!PyArg_ParseTuple(args, "O:process", &input_object)) {
    return NULL;
  }
  if (!get_data_view(input_object, &input)) {
    return NULL;
  }

  self->processing = 1;
  ret = compress_stream(self, BROTLI_OPERATION_PROCESS, (uint8_t*)input.buf,
                        input.len);
  PyBuffer_Release(&input);
  self->processing = 0;
  return ret;
}

static PyObject* brotli_Compressor_flush(PyBrotli_Compressor* self) {
  PyObject* self_type = (PyObject*)PY_GET_TYPE((PyObject*)self);
  PyObject* ret = NULL;

  if (self->healthy == 0) {
    set_brotli_exception(self_type, kCompressUnhealthyError);
    return NULL;
  }
  if (self->processing != 0) {
    set_brotli_exception(self_type, kCompressConcurrentError);
    return NULL;
  }

  self->processing = 1;
  ret = compress_stream(self, BROTLI_OPERATION_FLUSH, NULL, 0);
  self->processing = 0;
  return ret;
}

static PyObject* brotli_Compressor_finish(PyBrotli_Compressor* self) {
  PyObject* self_type = (PyObject*)PY_GET_TYPE((PyObject*)self);
  PyObject* ret = NULL;

  if (self->healthy == 0) {
    set_brotli_exception(self_type, kCompressUnhealthyError);
    return NULL;
  }
  if (self->processing != 0) {
    set_brotli_exception(self_type, kCompressConcurrentError);
    return NULL;
  }

  self->processing = 1;
  ret = compress_stream(self, BROTLI_OPERATION_FINISH, NULL, 0);
  self->processing = 0;
  if (ret != NULL) {
    assert(BrotliEncoderIsFinished(self->enc));
  }
  return ret;
}

/* --- Decompressor --- */

typedef struct {
  PyObject_HEAD BrotliDecoderState* dec;
  uint8_t* unconsumed_data;
  size_t unconsumed_data_length;
  int healthy;
  int processing;
} PyBrotli_Decompressor;

static PyObject* brotli_Decompressor_new(PyTypeObject* type, PyObject* args,
                                         PyObject* keywds) {
  /* `tp_itemsize` is 0, so `nitems` could be 0. */
  PyBrotli_Decompressor* self = (PyBrotli_Decompressor*)type->tp_alloc(type, 0);
  PyObject* self_type = (PyObject*)type;

  if (self == NULL) return NULL;
  self->healthy = 0;
  self->processing = 0;

  self->dec = BrotliDecoderCreateInstance(0, 0, 0);
  if (self->dec == NULL) {
    set_brotli_exception(self_type, kDecompressCreateError);
    PY_GET_TYPE(self)->tp_free((PyObject*)self);
    return NULL;
  }

  self->unconsumed_data = NULL;
  self->unconsumed_data_length = 0;
  self->healthy = 1;

  return (PyObject*)self;
}

static int brotli_Decompressor_init(PyBrotli_Decompressor* self, PyObject* args,
                                    PyObject* keywds) {
  static const char* kwlist[] = {NULL};

  int ok = PyArg_ParseTupleAndKeywords(args, keywds, "|:Decompressor",
                                       (char**)kwlist);

  assert(self->healthy);

  if (!ok) {
    self->healthy = 0;
    return -1;
  }

  return 0;
}

static void brotli_Decompressor_dealloc(PyBrotli_Decompressor* self) {
  if (self->dec) BrotliDecoderDestroyInstance(self->dec);
  if (self->unconsumed_data) {
    free(self->unconsumed_data);
    self->unconsumed_data = NULL;
  }
  PY_GET_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* brotli_Decompressor_process(PyBrotli_Decompressor* self,
                                             PyObject* args, PyObject* keywds) {
  static const char* kwlist[] = {"", "output_buffer_limit", NULL};

  PyObject* self_type = (PyObject*)PY_GET_TYPE((PyObject*)self);
  PyObject* ret = NULL;
  PyObject* input_object = NULL;
  Py_buffer input;
  Py_ssize_t output_buffer_limit = PY_SSIZE_T_MAX;
  const uint8_t* next_in = NULL;
  size_t avail_in = 0;
  BrotliDecoderResult result = BROTLI_DECODER_RESULT_ERROR;
  Buffer buffer;
  uint8_t* new_tail = NULL;
  size_t new_tail_length = 0;
  int oom = 0;

  if (self->healthy == 0) {
    set_brotli_exception(self_type, kDecompressUnhealthyError);
    return NULL;
  }
  if (self->processing != 0) {
    set_brotli_exception(self_type, kDecompressConcurrentError);
    return NULL;
  }

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "O|n:process", (char**)kwlist,
                                   &input_object, &output_buffer_limit)) {
    return NULL;
  }
  if (!get_data_view(input_object, &input)) {
    return NULL;
  }

  Buffer_Init(&buffer);
  self->processing = 1;

  if (self->unconsumed_data_length > 0) {
    if (input.len > 0) {
      set_brotli_exception(self_type, kDecompressSinkError);
      goto finally;
    }
    next_in = self->unconsumed_data;
    avail_in = self->unconsumed_data_length;
  } else {
    next_in = (uint8_t*)input.buf;
    avail_in = input.len;
  }

  /* While not guaranteed, we expect that some output will be produced. */
  if (Buffer_Grow(&buffer) < 0) {
    oom = 1;
    goto finally;
  }

  Py_BEGIN_ALLOW_THREADS;
  while (1) {
    result = BrotliDecoderDecompressStream(self->dec, &avail_in, &next_in,
                                           &buffer.avail_out, &buffer.next_out,
                                           NULL);

    if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
      assert(buffer.avail_out == 0);
      /* All allocated is used -> reached the output length limit. */
      if (buffer.total_allocated >= output_buffer_limit) break;
      if (Buffer_Grow(&buffer) < 0) {
        oom = 1;
        break;
      }
      continue;
    }
    break;
  }
  Py_END_ALLOW_THREADS;

  if (oom) {
    goto finally;
  } else if (result == BROTLI_DECODER_RESULT_ERROR) {
    set_brotli_exception(self_type, kDecompressError);
    goto finally;
  }

  /* Here result is either SUCCESS / NEEDS_MORE_INPUT / NEEDS_MORE_OUTPUT (if
     `output_buffer_limit` was reached). */

  if (avail_in > 0) {
    new_tail = malloc(avail_in);
    if (new_tail == NULL) {
      oom = 1;
      goto finally;
    }
    memcpy(new_tail, next_in, avail_in);
    new_tail_length = avail_in;
  }

  if ((result == BROTLI_DECODER_RESULT_SUCCESS) && (avail_in > 0)) {
    /* TODO(eustas): Add API to ignore / fetch unused "tail"? */
    set_brotli_exception(self_type, kDecompressError);
    goto finally;
  }

  ret = Buffer_Finish(&buffer);
  if (ret == NULL) oom = 1;

finally:
  if (oom) {
    PyErr_SetString(PyExc_MemoryError, kOomError);
    assert(ret == NULL);
  }
  PyBuffer_Release(&input);
  Buffer_Cleanup(&buffer);
  if (self->unconsumed_data) {
    free(self->unconsumed_data);
    self->unconsumed_data = NULL;
  }
  self->unconsumed_data = new_tail;
  self->unconsumed_data_length = new_tail_length;
  if (PyErr_Occurred() != NULL) {
    assert(ret == NULL);
    self->healthy = 0;
  }
  self->processing = 0;
  return ret;
}

static PyObject* brotli_Decompressor_is_finished(PyBrotli_Decompressor* self) {
  PyObject* self_type = (PyObject*)PY_GET_TYPE((PyObject*)self);
  if (self->healthy == 0) {
    set_brotli_exception(self_type, kDecompressUnhealthyError);
    return NULL;
  }
  if (self->processing != 0) {
    set_brotli_exception(self_type, kDecompressConcurrentError);
    return NULL;
  }
  if (BrotliDecoderIsFinished(self->dec)) {
    Py_RETURN_TRUE;
  } else {
    Py_RETURN_FALSE;
  }
}

static PyObject* brotli_Decompressor_can_accept_more_data(
    PyBrotli_Decompressor* self) {
  PyObject* self_type = (PyObject*)PY_GET_TYPE((PyObject*)self);
  if (self->healthy == 0) {
    set_brotli_exception(self_type, kDecompressUnhealthyError);
    return NULL;
  }
  if (self->processing != 0) {
    set_brotli_exception(self_type, kDecompressConcurrentError);
    return NULL;
  }
  if (self->unconsumed_data_length > 0) {
    Py_RETURN_FALSE;
  } else {
    Py_RETURN_TRUE;
  }
}

/* --- Module functions --- */

static PyObject* brotli_decompress(PyObject* m, PyObject* args,
                                   PyObject* keywds) {
  static const char* kwlist[] = {"string", NULL};

  BrotliDecoderState* state = NULL;
  BrotliDecoderResult result = BROTLI_DECODER_RESULT_ERROR;
  const uint8_t* next_in = NULL;
  size_t available_in = 0;
  Buffer buffer;
  PyObject* ret = NULL;
  PyObject* input_object = NULL;
  Py_buffer input;
  int oom = 0;

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "O|:decompress",
                                   (char**)kwlist, &input_object)) {
    return NULL;
  }
  if (!get_data_view(input_object, &input)) {
    return NULL;
  }

  Buffer_Init(&buffer);

  next_in = (uint8_t*)input.buf;
  available_in = input.len;

  state = BrotliDecoderCreateInstance(0, 0, 0);
  if (state == NULL) {
    oom = 1;
    goto finally;
  }

  if (Buffer_Grow(&buffer) < 0) {
    oom = 1;
    goto finally;
  }

  Py_BEGIN_ALLOW_THREADS;
  while (1) {
    result = BrotliDecoderDecompressStream(
        state, &available_in, &next_in, &buffer.avail_out, &buffer.next_out, 0);
    if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
      assert(buffer.avail_out == 0);
      if (Buffer_Grow(&buffer) < 0) {
        oom = 1;
        break;
      }
      continue;
    }
    break;
  }
  Py_END_ALLOW_THREADS;

  if (oom) {
    goto finally;
  } else if (result != BROTLI_DECODER_RESULT_SUCCESS || available_in > 0) {
    set_brotli_exception_from_module(m, kDecompressError);
    goto finally;
  }

  ret = Buffer_Finish(&buffer);
  if (ret == NULL) oom = 1;

finally:
  if (oom) PyErr_SetString(PyExc_MemoryError, kOomError);
  PyBuffer_Release(&input);
  Buffer_Cleanup(&buffer);
  if (state) BrotliDecoderDestroyInstance(state);
  return ret;
}

/* Module definition */

static PyMethodDef brotli_methods[] = {
    {"decompress", (PyCFunction)brotli_decompress, METH_VARARGS | METH_KEYWORDS,
     brotli_decompress__doc__},
    {NULL, NULL, 0, NULL}};

static PyMethodDef brotli_Compressor_methods[] = {
    {"process", (PyCFunction)brotli_Compressor_process, METH_VARARGS,
     brotli_Compressor_process_doc},
    {"flush", (PyCFunction)brotli_Compressor_flush, METH_NOARGS,
     brotli_Compressor_flush_doc},
    {"finish", (PyCFunction)brotli_Compressor_finish, METH_NOARGS,
     brotli_Compressor_finish_doc},
    {NULL} /* Sentinel */
};

static PyMethodDef brotli_Decompressor_methods[] = {
    {"process", (PyCFunction)brotli_Decompressor_process,
     METH_VARARGS | METH_KEYWORDS, brotli_Decompressor_process_doc},
    {"is_finished", (PyCFunction)brotli_Decompressor_is_finished, METH_NOARGS,
     brotli_Decompressor_is_finished_doc},
    {"can_accept_more_data",
     (PyCFunction)brotli_Decompressor_can_accept_more_data, METH_NOARGS,
     brotli_Decompressor_can_accept_more_data_doc},
    {NULL} /* Sentinel */
};

#if PY_MAJOR_VERSION >= 3

static struct PyModuleDef brotli_module = {
    PyModuleDef_HEAD_INIT,
    "_brotli",      /* m_name */
    brotli_doc,     /* m_doc */
    0,              /* m_size */
    brotli_methods, /* m_methods */
    NULL,           /* m_reload */
    NULL,           /* m_traverse */
    NULL,           /* m_clear */
    NULL            /* m_free */
};

static PyType_Slot brotli_Compressor_slots[] = {
    {Py_tp_dealloc, (destructor)brotli_Compressor_dealloc},
    {Py_tp_doc, (void*)brotli_Compressor_doc},
    {Py_tp_methods, brotli_Compressor_methods},
    {Py_tp_init, (initproc)brotli_Compressor_init},
    {Py_tp_new, brotli_Compressor_new},
    {0, 0},
};

static PyType_Spec brotli_Compressor_spec = {
    "brotli.Compressor", sizeof(PyBrotli_Compressor), 0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, brotli_Compressor_slots};

static PyType_Slot brotli_Decompressor_slots[] = {
    {Py_tp_dealloc, (destructor)brotli_Decompressor_dealloc},
    {Py_tp_doc, (void*)brotli_Decompressor_doc},
    {Py_tp_methods, brotli_Decompressor_methods},
    {Py_tp_init, (initproc)brotli_Decompressor_init},
    {Py_tp_new, brotli_Decompressor_new},
    {0, 0},
};

static PyType_Spec brotli_Decompressor_spec = {
    "brotli.Decompressor", sizeof(PyBrotli_Decompressor), 0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, brotli_Decompressor_slots};

#else

static PyTypeObject brotli_CompressorType = {
    PyObject_HEAD_INIT(NULL) 0,            /* ob_size */
    "brotli.Compressor",                   /* tp_name */
    sizeof(PyBrotli_Compressor),           /* tp_basicsize */
    0,                                     /* tp_itemsize */
    (destructor)brotli_Compressor_dealloc, /* tp_dealloc */
    0,                                     /* tp_print */
    0,                                     /* tp_getattr */
    0,                                     /* tp_setattr */
    0,                                     /* tp_compare */
    0,                                     /* tp_repr */
    0,                                     /* tp_as_number */
    0,                                     /* tp_as_sequence */
    0,                                     /* tp_as_mapping */
    0,                                     /* tp_hash  */
    0,                                     /* tp_call */
    0,                                     /* tp_str */
    0,                                     /* tp_getattro */
    0,                                     /* tp_setattro */
    0,                                     /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                    /* tp_flags */
    brotli_Compressor_doc,                 /* tp_doc */
    0,                                     /* tp_traverse */
    0,                                     /* tp_clear */
    0,                                     /* tp_richcompare */
    0,                                     /* tp_weaklistoffset */
    0,                                     /* tp_iter */
    0,                                     /* tp_iternext */
    brotli_Compressor_methods,             /* tp_methods */
    0,                                     /* tp_members */
    0,                                     /* tp_getset */
    0,                                     /* tp_base */
    0,                                     /* tp_dict */
    0,                                     /* tp_descr_get */
    0,                                     /* tp_descr_set */
    0,                                     /* tp_dictoffset */
    (initproc)brotli_Compressor_init,      /* tp_init */
    0,                                     /* tp_alloc */
    brotli_Compressor_new,                 /* tp_new */
};

static PyTypeObject brotli_DecompressorType = {
    PyObject_HEAD_INIT(NULL) 0,              /* ob_size */
    "brotli.Decompressor",                   /* tp_name */
    sizeof(PyBrotli_Decompressor),           /* tp_basicsize */
    0,                                       /* tp_itemsize */
    (destructor)brotli_Decompressor_dealloc, /* tp_dealloc */
    0,                                       /* tp_print */
    0,                                       /* tp_getattr */
    0,                                       /* tp_setattr */
    0,                                       /* tp_compare */
    0,                                       /* tp_repr */
    0,                                       /* tp_as_number */
    0,                                       /* tp_as_sequence */
    0,                                       /* tp_as_mapping */
    0,                                       /* tp_hash  */
    0,                                       /* tp_call */
    0,                                       /* tp_str */
    0,                                       /* tp_getattro */
    0,                                       /* tp_setattro */
    0,                                       /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                      /* tp_flags */
    brotli_Decompressor_doc,                 /* tp_doc */
    0,                                       /* tp_traverse */
    0,                                       /* tp_clear */
    0,                                       /* tp_richcompare */
    0,                                       /* tp_weaklistoffset */
    0,                                       /* tp_iter */
    0,                                       /* tp_iternext */
    brotli_Decompressor_methods,             /* tp_methods */
    0,                                       /* tp_members */
    0,                                       /* tp_getset */
    0,                                       /* tp_base */
    0,                                       /* tp_dict */
    0,                                       /* tp_descr_get */
    0,                                       /* tp_descr_set */
    0,                                       /* tp_dictoffset */
    (initproc)brotli_Decompressor_init,      /* tp_init */
    0,                                       /* tp_alloc */
    brotli_Decompressor_new,                 /* tp_new */
};

#endif

#if PY_MAJOR_VERSION >= 3
#define INIT_BROTLI PyInit__brotli
#define CREATE_BROTLI PyModule_Create(&brotli_module)
#define RETURN_BROTLI return m
#define RETURN_NULL return NULL
#else
#define INIT_BROTLI init_brotli
#define CREATE_BROTLI Py_InitModule3("_brotli", brotli_methods, brotli_doc)
#define RETURN_BROTLI return
#define RETURN_NULL return
#endif

/* Emulates PyModule_AddObject */
static int RegisterObject(PyObject* mod, const char* name, PyObject* value) {
  assert(value != NULL);
#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 10
  int ret = PyModule_AddObjectRef(mod, name, value);
  /* Emulates PyModule_AddObject, i.e. decrements the reference count on
     success. */
  if (ret == 0) Py_DECREF(value);
  return ret;
#else
  return PyModule_AddObject(mod, name, value);
#endif
}

PyMODINIT_FUNC INIT_BROTLI(void) {
  PyObject* m = CREATE_BROTLI;
  PyObject* error_type = NULL;
  PyObject* compressor_type = NULL;
  PyObject* decompressor_type = NULL;

  if (m == NULL) goto error;

  error_type = PyErr_NewExceptionWithDoc((char*)"brotli.error",
                                         brotli_error_doc, NULL, NULL);
  if (error_type == NULL) goto error;

  if (RegisterObject(m, kErrorAttr, error_type) < 0) goto error;
#if PY_MAJOR_VERSION < 3
  /* Assumption: pointer is used only while module is alive and well. */
  BrotliError = error_type;
#endif
  error_type = NULL;

#if PY_MAJOR_VERSION >= 3
  compressor_type = PyType_FromSpec(&brotli_Compressor_spec);
  decompressor_type = PyType_FromSpec(&brotli_Decompressor_spec);
#else
  compressor_type = (PyObject*)&brotli_CompressorType;
  Py_INCREF(compressor_type);
  decompressor_type = (PyObject*)&brotli_DecompressorType;
  Py_INCREF(decompressor_type);
#endif
  if (compressor_type == NULL) goto error;
  if (PyType_Ready((PyTypeObject*)compressor_type) < 0) goto error;
#if PY_MAJOR_VERSION >= 3
  if (PyObject_SetAttrString(compressor_type, kModuleAttr, m) < 0) goto error;
#endif
  if (RegisterObject(m, "Compressor", compressor_type) < 0) goto error;
  compressor_type = NULL;

  if (decompressor_type == NULL) goto error;
  if (PyType_Ready((PyTypeObject*)decompressor_type) < 0) goto error;
#if PY_MAJOR_VERSION >= 3
  if (PyObject_SetAttrString(decompressor_type, kModuleAttr, m) < 0) goto error;
#endif
  if (RegisterObject(m, "Decompressor", decompressor_type) < 0) goto error;
  decompressor_type = NULL;

  PyModule_AddIntConstant(m, "MODE_GENERIC", (int)BROTLI_MODE_GENERIC);
  PyModule_AddIntConstant(m, "MODE_TEXT", (int)BROTLI_MODE_TEXT);
  PyModule_AddIntConstant(m, "MODE_FONT", (int)BROTLI_MODE_FONT);

  char version[16]; /* 3 + 1 + 4 + 1 + 4 + 1 == 14 */
  uint32_t decoderVersion = BrotliDecoderVersion();
  snprintf(version, sizeof(version), "%d.%d.%d", decoderVersion >> 24,
           (decoderVersion >> 12) & 0xFFF, decoderVersion & 0xFFF);
  PyModule_AddStringConstant(m, "__version__", version);

  RETURN_BROTLI;

error:
  if (m != NULL) {
    Py_DECREF(m);
    m = NULL;
  }
  if (error_type != NULL) {
    Py_DECREF(error_type);
    error_type = NULL;
  }
  if (compressor_type != NULL) {
    Py_DECREF(compressor_type);
    compressor_type = NULL;
  }
  if (decompressor_type != NULL) {
    Py_DECREF(decompressor_type);
    decompressor_type = NULL;
  }
  RETURN_NULL;
}
