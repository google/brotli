#define PY_SSIZE_T_CLEAN 1
#include <Python.h>
#include <bytesobject.h>
#include <structmember.h>
#include <cstdio>
#include <vector>
#include "../common/version.h"
#include <brotli/decode.h>
#include <brotli/encode.h>

#if PY_MAJOR_VERSION >= 3
#define PyInt_Check PyLong_Check
#define PyInt_AsLong PyLong_AsLong
#endif

static PyObject *BrotliError;

static int as_bounded_int(PyObject *o, int* result, int lower_bound, int upper_bound) {
  long value = PyInt_AsLong(o);
  if ((value < (long) lower_bound) || (value > (long) upper_bound)) {
    return 0;
  }
  *result = (int) value;
  return 1;
}

static int mode_convertor(PyObject *o, BrotliEncoderMode *mode) {
  if (!PyInt_Check(o)) {
    PyErr_SetString(BrotliError, "Invalid mode");
    return 0;
  }

  int mode_value = -1;
  if (!as_bounded_int(o, &mode_value, 0, 255)) {
    PyErr_SetString(BrotliError, "Invalid mode");
    return 0;
  }
  *mode = (BrotliEncoderMode) mode_value;
  if (*mode != BROTLI_MODE_GENERIC &&
      *mode != BROTLI_MODE_TEXT &&
      *mode != BROTLI_MODE_FONT) {
    PyErr_SetString(BrotliError, "Invalid mode");
    return 0;
  }

  return 1;
}

static int quality_convertor(PyObject *o, int *quality) {
  if (!PyInt_Check(o)) {
    PyErr_SetString(BrotliError, "Invalid quality");
    return 0;
  }

  if (!as_bounded_int(o, quality, 0, 11)) {
    PyErr_SetString(BrotliError, "Invalid quality. Range is 0 to 11.");
    return 0;
  }

  return 1;
}

static int lgwin_convertor(PyObject *o, int *lgwin) {
  if (!PyInt_Check(o)) {
    PyErr_SetString(BrotliError, "Invalid lgwin");
    return 0;
  }

  if (!as_bounded_int(o, lgwin, 10, 24)) {
    PyErr_SetString(BrotliError, "Invalid lgwin. Range is 10 to 24.");
    return 0;
  }

  return 1;
}

static int lgblock_convertor(PyObject *o, int *lgblock) {
  if (!PyInt_Check(o)) {
    PyErr_SetString(BrotliError, "Invalid lgblock");
    return 0;
  }

  if (!as_bounded_int(o, lgblock, 0, 24) || (*lgblock != 0 && *lgblock < 16)) {
    PyErr_SetString(BrotliError, "Invalid lgblock. Can be 0 or in range 16 to 24.");
    return 0;
  }

  return 1;
}

PyDoc_STRVAR(brotli_Compressor_doc,
"An object to compress a byte string.\n"
"\n"
"Signature:\n"
"  Compressor(mode=MODE_GENERIC, quality=11, lgwin=22, lgblock=0, dictionary='')\n"
"\n"
"Args:\n"
"  mode (int, optional): The compression mode can be MODE_GENERIC (default),\n"
"    MODE_TEXT (for UTF-8 format text input) or MODE_FONT (for WOFF 2.0). \n"
"  quality (int, optional): Controls the compression-speed vs compression-\n"
"    density tradeoff. The higher the quality, the slower the compression.\n"
"    Range is 0 to 11. Defaults to 11.\n"
"  lgwin (int, optional): Base 2 logarithm of the sliding window size. Range\n"
"    is 10 to 24. Defaults to 22.\n"
"  lgblock (int, optional): Base 2 logarithm of the maximum input block size.\n"
"    Range is 16 to 24. If set to 0, the value will be set based on the\n"
"    quality. Defaults to 0.\n"
"  dictionary (bytes, optional): Custom dictionary. Only last sliding window\n"
"     size bytes will be used.\n"
"\n"
"Raises:\n"
"  brotli.error: If arguments are invalid.\n");

typedef struct {
  PyObject_HEAD
  BrotliEncoderState* enc;
} brotli_Compressor;

static void brotli_Compressor_dealloc(brotli_Compressor* self) {
  BrotliEncoderDestroyInstance(self->enc);
  #if PY_MAJOR_VERSION >= 3
  Py_TYPE(self)->tp_free((PyObject*)self);
  #else
  self->ob_type->tp_free((PyObject*)self);
  #endif
}

static PyObject* brotli_Compressor_new(PyTypeObject *type, PyObject *args, PyObject *keywds) {
  brotli_Compressor *self;
  self = (brotli_Compressor *)type->tp_alloc(type, 0);

  if (self != NULL) {
    self->enc = BrotliEncoderCreateInstance(0, 0, 0);
  }

  return (PyObject *)self;
}

static int brotli_Compressor_init(brotli_Compressor *self, PyObject *args, PyObject *keywds) {
  BrotliEncoderMode mode = (BrotliEncoderMode) -1;
  int quality = -1;
  int lgwin = -1;
  int lgblock = -1;
  uint8_t* custom_dictionary = NULL;
  size_t custom_dictionary_length = 0;
  int ok;

  static const char *kwlist[] = {
      "mode", "quality", "lgwin", "lgblock", "dictionary", NULL};

  ok = PyArg_ParseTupleAndKeywords(args, keywds, "|O&O&O&O&s#:Compressor",
                    const_cast<char **>(kwlist),
                    &mode_convertor, &mode,
                    &quality_convertor, &quality,
                    &lgwin_convertor, &lgwin,
                    &lgblock_convertor, &lgblock,
                    &custom_dictionary, &custom_dictionary_length);
  if (!ok)
    return -1;
  if (!self->enc)
    return -1;

  if ((int) mode != -1)
    BrotliEncoderSetParameter(self->enc, BROTLI_PARAM_MODE, (uint32_t)mode);
  if (quality != -1)
    BrotliEncoderSetParameter(self->enc, BROTLI_PARAM_QUALITY, (uint32_t)quality);
  if (lgwin != -1)
    BrotliEncoderSetParameter(self->enc, BROTLI_PARAM_LGWIN, (uint32_t)lgwin);
  if (lgblock != -1)
    BrotliEncoderSetParameter(self->enc, BROTLI_PARAM_LGBLOCK, (uint32_t)lgblock);

  if (custom_dictionary_length != 0) {
    BrotliEncoderSetCustomDictionary(self->enc, custom_dictionary_length,
                                     custom_dictionary);
  }

  return 0;
}

PyDoc_STRVAR(brotli_Compressor_compress_doc,
"Compress a byte string.\n"
"\n"
"Signature:\n"
"  compress(string)\n"
"\n"
"Args:\n"
"  string (bytes): The input data.\n"
"\n"
"Returns:\n"
"  The compressed byte string.\n"
"\n"
"Raises:\n"
"  brotli.error: If compression fails.\n");

static PyObject* brotli_Compressor_compress(brotli_Compressor *self, PyObject *args) {
  PyObject* ret = NULL;
  uint8_t* input;
  uint8_t* output = NULL;
  uint8_t* next_out;
  const uint8_t *next_in;
  size_t input_length;
  size_t output_length;
  size_t available_in;
  size_t available_out;
  int ok;

  ok = PyArg_ParseTuple(args, "s#:compress", &input, &input_length);
  if (!ok)
    return NULL;

  output_length = input_length + (input_length >> 2) + 10240;

  if (!self->enc) {
    ok = false;
    goto end;
  }

  output = new uint8_t[output_length];
  available_out = output_length;
  next_out = output;
  available_in = input_length;
  next_in = input;

  BrotliEncoderCompressStream(self->enc, BROTLI_OPERATION_FINISH,
                              &available_in, &next_in,
                              &available_out, &next_out, 0);
  ok = BrotliEncoderIsFinished(self->enc);

end:
  if (ok) {
    ret = PyBytes_FromStringAndSize((char*)output, output_length - available_out);
  } else {
    PyErr_SetString(BrotliError, "BrotliCompressBuffer failed");
  }

  delete[] output;

  return ret;
}

static PyMemberDef brotli_Compressor_members[] = {
  {NULL}  /* Sentinel */
};

static PyMethodDef brotli_Compressor_methods[] = {
  {"compress", (PyCFunction)brotli_Compressor_compress, METH_VARARGS, brotli_Compressor_compress_doc},
  {NULL}  /* Sentinel */
};

static PyTypeObject brotli_CompressorType = {
  #if PY_MAJOR_VERSION >= 3
  PyVarObject_HEAD_INIT(NULL, 0)
  #else
  PyObject_HEAD_INIT(NULL)
  0,                                     /* ob_size*/
  #endif
  "brotli.Compressor",                   /* tp_name */
  sizeof(brotli_Compressor),             /* tp_basicsize */
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
  brotli_Compressor_members,             /* tp_members */
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

PyDoc_STRVAR(brotli_decompress__doc__,
"Decompress a compressed byte string.\n"
"\n"
"Signature:\n"
"  decompress(string)\n"
"\n"
"Args:\n"
"  string (bytes): The compressed input data.\n"
"  dictionary (bytes, optional): Custom dictionary. MUST be the same data\n"
"     as passed to compress method.\n"
"\n"
"Returns:\n"
"  The decompressed byte string.\n"
"\n"
"Raises:\n"
"  brotli.error: If decompressor fails.\n");

static PyObject* brotli_decompress(PyObject *self, PyObject *args, PyObject *keywds) {
  PyObject *ret = NULL;
  const uint8_t *input, *custom_dictionary;
  size_t length, custom_dictionary_length;
  int ok;

  static const char *kwlist[] = {"string", "dictionary", NULL};

  custom_dictionary = NULL;
  custom_dictionary_length = 0;

  ok = PyArg_ParseTupleAndKeywords(args, keywds, "s#|s#:decompress",
                        const_cast<char **>(kwlist),
                        &input, &length,
                        &custom_dictionary, &custom_dictionary_length);
  if (!ok)
    return NULL;

  std::vector<uint8_t> output;
  const size_t kBufferSize = 65536;
  uint8_t* buffer = new uint8_t[kBufferSize];
  BrotliDecoderState* state = BrotliDecoderCreateInstance(0, 0, 0);
  if (custom_dictionary_length != 0) {
    BrotliDecoderSetCustomDictionary(state, custom_dictionary_length, custom_dictionary);
  }

  BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
  while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
    size_t available_out = kBufferSize;
    uint8_t* next_out = buffer;
    result = BrotliDecoderDecompressStream(state, &length, &input,
                                           &available_out, &next_out, 0);
    size_t used_out = kBufferSize - available_out;
    if (used_out != 0)
      output.insert(output.end(), buffer, buffer + used_out);
  }
  ok = result == BROTLI_DECODER_RESULT_SUCCESS;
  if (ok) {
    ret = PyBytes_FromStringAndSize((char*)(output.size() ? &output[0] : NULL), output.size());
  } else {
    PyErr_SetString(BrotliError, "BrotliDecompress failed");
  }

  BrotliDecoderDestroyInstance(state);
  delete[] buffer;

  return ret;
}

static PyMethodDef brotli_methods[] = {
  {"decompress", (PyCFunction)brotli_decompress, METH_VARARGS | METH_KEYWORDS, brotli_decompress__doc__},
  {NULL, NULL, 0, NULL}
};

PyDoc_STRVAR(brotli_doc, "Implementation module for the Brotli library.");

#if PY_MAJOR_VERSION >= 3
#define INIT_BROTLI   PyInit__brotli
#define CREATE_BROTLI PyModule_Create(&brotli_module)
#define RETURN_BROTLI return m
#define RETURN_NULL return NULL

static struct PyModuleDef brotli_module = {
  PyModuleDef_HEAD_INIT,
  "_brotli",
  brotli_doc,
  0,
  brotli_methods,
  NULL,
  NULL,
  NULL
};
#else
#define INIT_BROTLI   init_brotli
#define CREATE_BROTLI Py_InitModule3("_brotli", brotli_methods, brotli_doc)
#define RETURN_BROTLI return
#define RETURN_NULL return
#endif

PyMODINIT_FUNC INIT_BROTLI(void) {
  PyObject *m = CREATE_BROTLI;

  BrotliError = PyErr_NewException((char*) "brotli.error", NULL, NULL);
  if (BrotliError != NULL) {
    Py_INCREF(BrotliError);
    PyModule_AddObject(m, "error", BrotliError);
  }

  if (PyType_Ready(&brotli_CompressorType) < 0) {
    RETURN_NULL;
  }
  Py_INCREF(&brotli_CompressorType);
  PyModule_AddObject(m, "Compressor", (PyObject *)&brotli_CompressorType);

  PyModule_AddIntConstant(m, "MODE_GENERIC", (int) BROTLI_MODE_GENERIC);
  PyModule_AddIntConstant(m, "MODE_TEXT", (int) BROTLI_MODE_TEXT);
  PyModule_AddIntConstant(m, "MODE_FONT", (int) BROTLI_MODE_FONT);

  char version[16];
  snprintf(version, sizeof(version), "%d.%d.%d",
      BROTLI_VERSION >> 24, (BROTLI_VERSION >> 12) & 0xFFF, BROTLI_VERSION & 0xFFF);
  PyModule_AddStringConstant(m, "__version__", version);

  RETURN_BROTLI;
}
