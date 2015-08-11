#define PY_SSIZE_T_CLEAN 1
#include <Python.h>
#include <bytesobject.h>
#include "../enc/encode.h"
#include "../dec/decode.h"

#if PY_MAJOR_VERSION >= 3
#define PyInt_Check PyLong_Check
#define PyInt_AsLong PyLong_AsLong
#endif

#define BROTLI_VERSION "0.1.0"

using namespace brotli;

static PyObject *BrotliError;

static int mode_convertor(PyObject *o, BrotliParams::Mode *mode) {
  if (!PyInt_Check(o)) {
    PyErr_SetString(BrotliError, "Invalid mode");
    return 0;
  }

  *mode = (BrotliParams::Mode) PyInt_AsLong(o);
  if (*mode != BrotliParams::Mode::MODE_GENERIC &&
      *mode != BrotliParams::Mode::MODE_TEXT &&
      *mode != BrotliParams::Mode::MODE_FONT) {
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

  *quality = PyInt_AsLong(o);
  if (*quality < 0 || *quality > 11) {
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

  *lgwin = PyInt_AsLong(o);
  if (*lgwin < 16 || *lgwin > 24) {
    PyErr_SetString(BrotliError, "Invalid lgwin. Range is 16 to 24.");
    return 0;
  }

  return 1;
}

static int lgblock_convertor(PyObject *o, int *lgblock) {
  if (!PyInt_Check(o)) {
    PyErr_SetString(BrotliError, "Invalid lgblock");
    return 0;
  }

  *lgblock = PyInt_AsLong(o);
  if ((*lgblock != 0 && *lgblock < 16) || *lgblock > 24) {
    PyErr_SetString(BrotliError, "Invalid lgblock. Can be 0 or in range 16 to 24.");
    return 0;
  }

  return 1;
}

PyDoc_STRVAR(compress__doc__,
"Compress a byte string.\n"
"\n"
"Signature:\n"
"  compress(string, mode=MODE_GENERIC, quality=11, lgwin=22, lgblock=0)\n"
"\n"
"Args:\n"
"  string (bytes): The input data.\n"
"  mode (int, optional): The compression mode can be MODE_GENERIC (default),\n"
"    MODE_TEXT (for UTF-8 format text input) or MODE_FONT (for WOFF 2.0). \n"
"  quality (int, optional): Controls the compression-speed vs compression-\n"
"    density tradeoff. The higher the quality, the slower the compression.\n"
"    Range is 0 to 11. Defaults to 11.\n"
"  lgwin (int, optional): Base 2 logarithm of the sliding window size. Range\n"
"    is 16 to 24. Defaults to 22.\n"
"  lgblock (int, optional): Base 2 logarithm of the maximum input block size.\n"
"    Range is 16 to 24. If set to 0, the value will be set based on the\n"
"    quality. Defaults to 0.\n"
"\n"
"Returns:\n"
"  The compressed byte string.\n"
"\n"
"Raises:\n"
"  brotli.error: If arguments are invalid, or compressor fails.\n");

static PyObject* brotli_compress(PyObject *self, PyObject *args, PyObject *keywds) {
  PyObject *ret = NULL;
  uint8_t *input, *output;
  size_t length, output_length;
  BrotliParams::Mode mode = (BrotliParams::Mode) -1;
  int quality = -1;
  int lgwin = -1;
  int lgblock = -1;
  int ok;

  static const char *kwlist[] = {"string", "mode", "quality", "lgwin", "lgblock", NULL};

  ok = PyArg_ParseTupleAndKeywords(args, keywds, "s#|O&O&O&O&:compress",
                        const_cast<char **>(kwlist),
                        &input, &length,
                        &mode_convertor, &mode,
                        &quality_convertor, &quality,
                        &lgwin_convertor, &lgwin,
                        &lgblock_convertor, &lgblock);

  if (!ok)
    return NULL;

  output_length = 1.2 * length + 10240;
  output = new uint8_t[output_length];

  BrotliParams params;
  if (mode != -1)
    params.mode = mode;
  if (quality != -1)
    params.quality = quality;
  if (lgwin != -1)
    params.lgwin = lgwin;
  if (lgblock != -1)
    params.lgblock = lgblock;

  ok = BrotliCompressBuffer(params, length, input,
                            &output_length, output);
  if (ok) {
    ret = PyBytes_FromStringAndSize((char*)output, output_length);
  } else {
    PyErr_SetString(BrotliError, "BrotliCompressBuffer failed");
  }

  delete[] output;

  return ret;
}

int output_callback(void* data, const uint8_t* buf, size_t count) {
  std::vector<uint8_t> *output = (std::vector<uint8_t> *)data;
  output->insert(output->end(), buf, buf + count);
  return (int)count;
}

PyDoc_STRVAR(decompress__doc__,
"Decompress a compressed byte string.\n"
"\n"
"Signature:\n"
"  decompress(string)\n"
"\n"
"Args:\n"
"  string (bytes): The compressed input data.\n"
"\n"
"Returns:\n"
"  The decompressed byte string.\n"
"\n"
"Raises:\n"
"  brotli.error: If decompressor fails.\n");

static PyObject* brotli_decompress(PyObject *self, PyObject *args) {
  PyObject *ret = NULL;
  uint8_t *input;
  size_t length;
  int ok;

  ok = PyArg_ParseTuple(args, "s#:decompress", &input, &length);
  if (!ok)
    return NULL;

  BrotliMemInput memin;
  BrotliInput in = BrotliInitMemInput(input, length, &memin);

  BrotliOutput out;
  std::vector<uint8_t> output;
  out.cb_ = &output_callback;
  out.data_ = &output;

  ok = BrotliDecompress(in, out);
  if (ok) {
    ret = PyBytes_FromStringAndSize((char*)output.data(), output.size());
  } else {
    PyErr_SetString(BrotliError, "BrotliDecompress failed");
  }

  return ret;
}

static PyMethodDef brotli_methods[] = {
  {"compress",   (PyCFunction)brotli_compress, METH_VARARGS | METH_KEYWORDS, compress__doc__},
  {"decompress", brotli_decompress, METH_VARARGS, decompress__doc__},
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

  PyModule_AddIntConstant(m, "MODE_GENERIC", (int) BrotliParams::Mode::MODE_GENERIC);
  PyModule_AddIntConstant(m, "MODE_TEXT", (int) BrotliParams::Mode::MODE_TEXT);
  PyModule_AddIntConstant(m, "MODE_FONT", (int) BrotliParams::Mode::MODE_FONT);

  PyModule_AddStringConstant(m, "__version__", BROTLI_VERSION);

  RETURN_BROTLI;
}
