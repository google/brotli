#define PY_SSIZE_T_CLEAN 1
#include <Python.h>
#include <bytesobject.h>
#include "../enc/encode.h"
#include "../dec/decode.h"

#if PY_MAJOR_VERSION >= 3
#define PyInt_Check PyLong_Check
#define PyInt_AsLong PyLong_AsLong
#endif

using namespace brotli;

static PyObject *BrotliError;

static int mode_convertor(PyObject *o, BrotliParams::Mode *mode) {
  if (!PyInt_Check(o)) {
    PyErr_SetString(BrotliError, "Invalid mode");
    return 0;
  }

  *mode = (BrotliParams::Mode) PyInt_AsLong(o);
  if (*mode != BrotliParams::Mode::MODE_TEXT &&
      *mode != BrotliParams::Mode::MODE_FONT) {
    PyErr_SetString(BrotliError, "Invalid mode");
    return 0;
  }

  return 1;
}

PyDoc_STRVAR(compress__doc__,
"compress(string[, mode[, transform]]) -- Returned compressed string.\n"
"\n"
"Optional arg mode is the compression mode, either MODE_TEXT (default) or\n"
"MODE_FONT. Optional boolean arg transform controls whether to enable\n"
"encoder transforms or not, defaults to False.");

static PyObject* brotli_compress(PyObject *self, PyObject *args) {
  PyObject *ret = NULL;
  PyObject* transform = NULL;
  uint8_t *input, *output;
  size_t length, output_length;
  BrotliParams::Mode mode = (BrotliParams::Mode) -1;
  int ok;

  ok = PyArg_ParseTuple(args, "s#|O&O!:compress",
                        &input, &length,
                        &mode_convertor, &mode,
                        &PyBool_Type, &transform);

  if (!ok)
    return NULL;

  output_length = 1.2 * length + 10240;
  output = new uint8_t[output_length];

  BrotliParams params;
  if (mode != -1)
    params.mode = mode;
  if (transform)
    params.enable_transforms = PyObject_IsTrue(transform);

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
"decompress(string) -- Return decompressed string.");

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
  {"compress",   brotli_compress,   METH_VARARGS, compress__doc__},
  {"decompress", brotli_decompress, METH_VARARGS, decompress__doc__},
  {NULL, NULL, 0, NULL}
};

PyDoc_STRVAR(brotli__doc__,
"The functions in this module allow compression and decompression using the\n"
"Brotli library.\n"
"\n"
"compress(string[, mode, transform]) -- Compress string.\n"
"decompress(string) -- Decompresses a compressed string.\n");

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

  PyModule_AddIntConstant(m, "MODE_TEXT", (int) BrotliParams::Mode::MODE_TEXT);
  PyModule_AddIntConstant(m, "MODE_FONT", (int) BrotliParams::Mode::MODE_FONT);

  RETURN_BROTLI;
}
