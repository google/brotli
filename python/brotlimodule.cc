#define PY_SSIZE_T_CLEAN 1
#include <Python.h>
#include <bytesobject.h>
#include "../enc/encode.h"
#include "../dec/decode.h"
#include "../tools/version.h"

#if PY_MAJOR_VERSION >= 3
#define PyInt_Check PyLong_Check
#define PyInt_AsLong PyLong_AsLong
#endif

using namespace brotli;

static PyObject *BrotliError;

static int as_bounded_int(PyObject *o, int* result, int lower_bound, int upper_bound) {
  long value = PyInt_AsLong(o);
  if ((value < (long) lower_bound) || (value > (long) upper_bound)) {
    return 0;
  }
  *result = (int) value;
  return 1;
}

static int mode_convertor(PyObject *o, BrotliParams::Mode *mode) {
  if (!PyInt_Check(o)) {
    PyErr_SetString(BrotliError, "Invalid mode");
    return 0;
  }

  int mode_value = -1;
  if (!as_bounded_int(o, &mode_value, 0, 255)) {
    PyErr_SetString(BrotliError, "Invalid mode");
    return 0;
  }
  *mode = (BrotliParams::Mode) mode_value;
  if (*mode != BrotliParams::MODE_GENERIC &&
      *mode != BrotliParams::MODE_TEXT &&
      *mode != BrotliParams::MODE_FONT) {
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

PyDoc_STRVAR(compress__doc__,
"Compress a byte string.\n"
"\n"
"Signature:\n"
"  compress(string, mode=MODE_GENERIC, quality=11, lgwin=22, lgblock=0, dictionary='')\n"
"\n"
"Args:\n"
"  string (bytes): The input data.\n"
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
"Returns:\n"
"  The compressed byte string.\n"
"\n"
"Raises:\n"
"  brotli.error: If arguments are invalid, or compressor fails.\n");

static PyObject* brotli_compress(PyObject *self, PyObject *args, PyObject *keywds) {
  PyObject *ret = NULL;
  uint8_t *input, *output, *custom_dictionary;
  size_t length, output_length, custom_dictionary_length;
  BrotliParams::Mode mode = (BrotliParams::Mode) -1;
  int quality = -1;
  int lgwin = -1;
  int lgblock = -1;
  int ok;

  static const char *kwlist[] = {
      "string", "mode", "quality", "lgwin", "lgblock", "dictionary", NULL};

  custom_dictionary = NULL;
  custom_dictionary_length = 0;

  ok = PyArg_ParseTupleAndKeywords(args, keywds, "s#|O&O&O&O&s#:compress",
                        const_cast<char **>(kwlist),
                        &input, &length,
                        &mode_convertor, &mode,
                        &quality_convertor, &quality,
                        &lgwin_convertor, &lgwin,
                        &lgblock_convertor, &lgblock,
                        &custom_dictionary, &custom_dictionary_length);
  if (!ok)
    return NULL;

  output_length = length + (length >> 2) + 10240;
  output = new uint8_t[output_length];

  BrotliParams params;
  if ((int) mode != -1)
    params.mode = mode;
  if (quality != -1)
    params.quality = quality;
  if (lgwin != -1)
    params.lgwin = lgwin;
  if (lgblock != -1)
    params.lgblock = lgblock;

  if (custom_dictionary_length == 0) {
    ok = BrotliCompressBuffer(params, length, input,
                              &output_length, output);
  } else {
    uint8_t *custom_dictionary_start = custom_dictionary;
    BrotliMemIn in(input, length);
    BrotliMemOut out(output, output_length);
    size_t sliding_window_size = ((size_t)1) << params.lgwin;
    if (custom_dictionary_length > sliding_window_size) {
      custom_dictionary_start += custom_dictionary_length - sliding_window_size;
      custom_dictionary_length = sliding_window_size;
    }
    ok = BrotliCompressWithCustomDictionary(custom_dictionary_length,
        custom_dictionary_start, params, &in, &out);
    output_length = out.position();
  }

  if (ok) {
    ret = PyBytes_FromStringAndSize((char*)output, output_length);
  } else {
    PyErr_SetString(BrotliError, "BrotliCompressBuffer failed");
  }

  delete[] output;

  return ret;
}

PyDoc_STRVAR(decompress__doc__,
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
  BrotliState state;
  BrotliStateInit(&state);
  if (custom_dictionary_length != 0) {
    BrotliSetCustomDictionary(custom_dictionary_length, custom_dictionary, &state);
  }

  BrotliResult result = BROTLI_RESULT_NEEDS_MORE_OUTPUT;
  while (result == BROTLI_RESULT_NEEDS_MORE_OUTPUT) {
    size_t available_out = kBufferSize;
    uint8_t* next_out = buffer;
    size_t total_out = 0;
    result = BrotliDecompressStream(&length, &input,
                                    &available_out, &next_out,
                                    &total_out, &state);
    size_t used_out = kBufferSize - available_out;
    if (used_out != 0)
      output.insert(output.end(), buffer, buffer + used_out);
  }
  ok = result == BROTLI_RESULT_SUCCESS;
  if (ok) {
    ret = PyBytes_FromStringAndSize((char*)(output.size() ? &output[0] : NULL), output.size());
  } else {
    PyErr_SetString(BrotliError, "BrotliDecompress failed");
  }
  
  BrotliStateCleanup(&state);
  delete[] buffer;

  return ret;
}

static PyMethodDef brotli_methods[] = {
  {"compress",   (PyCFunction)brotli_compress, METH_VARARGS | METH_KEYWORDS, compress__doc__},
  {"decompress", (PyCFunction)brotli_decompress, METH_VARARGS | METH_KEYWORDS, decompress__doc__},
  {NULL, NULL, 0, NULL}
};

PyDoc_STRVAR(brotli__doc__,
"The functions in this module allow compression and decompression using the\n"
"Brotli library.\n\n");

#if PY_MAJOR_VERSION >= 3
#define INIT_BROTLI   PyInit_brotli
#define CREATE_BROTLI PyModule_Create(&brotli_module)
#define RETURN_BROTLI return m

static struct PyModuleDef brotli_module = {
  PyModuleDef_HEAD_INIT,
  "brotli",
  brotli__doc__,
  0,
  brotli_methods,
  NULL,
  NULL,
  NULL
};
#else
#define INIT_BROTLI   initbrotli
#define CREATE_BROTLI Py_InitModule3("brotli", brotli_methods, brotli__doc__)
#define RETURN_BROTLI return
#endif

PyMODINIT_FUNC INIT_BROTLI(void) {
  PyObject *m = CREATE_BROTLI;

  BrotliError = PyErr_NewException((char*) "brotli.error", NULL, NULL);

  if (BrotliError != NULL) {
    Py_INCREF(BrotliError);
    PyModule_AddObject(m, "error", BrotliError);
  }

  PyModule_AddIntConstant(m, "MODE_GENERIC", (int) BrotliParams::MODE_GENERIC);
  PyModule_AddIntConstant(m, "MODE_TEXT", (int) BrotliParams::MODE_TEXT);
  PyModule_AddIntConstant(m, "MODE_FONT", (int) BrotliParams::MODE_FONT);

  PyModule_AddStringConstant(m, "__version__", BROTLI_VERSION);

  RETURN_BROTLI;
}
