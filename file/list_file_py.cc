// Copyright 2015, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//
// Example script:
/*
   import list_file_py

   a = list_file_py.Reader('secret.lst')
   for str in a:
     print str
*/
// If Python.h is not found, please install python-dev
#include <Python.h>
#include <structmember.h>

#include "file/file.h"
#include "file/filesource.h"
#include "file/list_file.h"
#include "util/compressors.h"
#include "util/lz4_compressor.h"

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#pragma GCC diagnostic ignored "-Wwrite-strings"

using std::string;

static PyObject* list_exception;
static char list_exception_name[] = "list_file_py.Error";


static PyMethodDef ListMethods[] = {
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


typedef struct {
    PyObject_HEAD
    int number;
    file::ListReader* reader;
} Reader;

static void Reader_dealloc(Reader* self) {
  delete self->reader;
  self->ob_type->tp_free((PyObject*)self);
}

static PyObject* Reader_new(PyTypeObject* type, PyObject *args, PyObject *kwds) {
  Reader *self = (Reader *)type->tp_alloc(type, 0);
  if (self == NULL)
    return NULL;

  self->reader = NULL;

  return (PyObject *)self;
}

static int Reader_init(Reader* self, PyObject *args, PyObject *kwds) {
  const char *name;
  if (!PyArg_ParseTuple(args, "s", &name))
    return -1;
  file::ReadonlyFile::Options opts;
  opts.use_mmap = false;
  auto res = file::ReadonlyFile::Open(name, opts);
  if (!res.ok()) {
    PyErr_Format(PyExc_ValueError, "Can not open file %s", name);
    return -1;
  }
  self->reader = new file::ListReader(res.obj, TAKE_OWNERSHIP);
  return 0;
}

static PyObject* Reader_next(PyObject* obj) {
  Reader* self = (Reader*)obj;
  CHECK_NOTNULL(self);
  CHECK_NOTNULL(self->reader);

  PyObject* res = NULL;
  std::string record_buf;
  strings::Slice record;
  if (!self->reader->ReadRecord(&record, &record_buf)) {
    PyErr_SetObject(PyExc_StopIteration, Py_None);
    return NULL;
  }

  res = PyBytes_FromStringAndSize(record.data(), record.size());
  return res;
}

static PyMethodDef reader_methods[] = {
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static PyMemberDef reader_members[] = {
    {"number", T_INT, offsetof(Reader, number), 0,
     "Reader number"},
    {NULL}  /* Sentinel */
};

static PyTypeObject ReaderType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "list_file_py.Reader",             /*tp_name*/
    sizeof(Reader),             /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Reader_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT, /*tp_flags*/
    "Reader objects",           /* tp_doc */
    0,                   /* tp_traverse */
    0,                   /* tp_clear */
    0,                   /* tp_richcompare */
    0,                   /* tp_weaklistoffset */
    PyObject_SelfIter,   /* tp_iter */
    Reader_next,         /* tp_iternext */
    reader_methods,             /* tp_methods */
    reader_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Reader_init,      /* tp_init */
    0,                         /* tp_alloc */
    Reader_new,                 /* tp_new */
};

typedef struct {
    PyObject_HEAD
    file::ListWriter *writer;
} Writer;

static void Writer_dealloc(Writer *self) {
  delete self->writer;
  self->ob_type->tp_free((PyObject*)self);
}

static int Writer_init(Writer *self, PyObject *args, PyObject *kwds) {
  const char *path;
  if (!PyArg_ParseTuple(args, "s", &path))
    return -1;
  auto file = file::Open(path);
  if (!file) {
    PyErr_Format(PyExc_ValueError, "Can not open file %s", path);
    return -1;
  }
  self->writer = new file::ListWriter(new file::Sink(file, TAKE_OWNERSHIP));
  if (!self->writer->Init().ok()) {
    PyErr_Format(PyExc_RuntimeError, "Failure initializing ListWriter");
    return -1;
  }
  return 0;
}

static PyObject *Writer_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
  Writer *self = (Writer *)type->tp_alloc(type, 0);
  if (self == NULL)
    return NULL;

  self->writer = nullptr;

  return (PyObject *)self;
}

static PyObject *Writer_write(PyObject *self0, PyObject *args) {
  Writer *self = (Writer*)self0;
  CHECK_NOTNULL(self);
  CHECK_NOTNULL(self->writer);

  const char *data;
  Py_ssize_t data_len;
  if (!PyArg_ParseTuple(args, "s#", &data, &data_len))
    return NULL;
  if (!self->writer->AddRecord(StringPiece(data, data_len)).ok()) {
    PyErr_Format(PyExc_RuntimeError, "AddRecord failed");
    return NULL;
  }
  return Py_BuildValue("");
}

static PyObject *Writer_flush(PyObject *self0, PyObject *args) {
  Writer *self = (Writer*)self0;
  CHECK_NOTNULL(self);
  CHECK_NOTNULL(self->writer);

  if (!self->writer->Flush().ok()) {
    PyErr_Format(PyExc_RuntimeError, "Flush failed");
    return NULL;
  }
  return Py_BuildValue("");
}

static PyMethodDef writer_methods[] = {
    {"write", Writer_write, METH_VARARGS, "Writes an entry to the listfile"},
    {"flush", Writer_flush, METH_VARARGS, "Flushes to disk"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static PyTypeObject WriterType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "list_file_py.Writer",             /*tp_name*/
    sizeof(Writer),             /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Writer_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT, /*tp_flags*/
    "Writer objects",           /* tp_doc */
    0,                   /* tp_traverse */
    0,                   /* tp_clear */
    0,                   /* tp_richcompare */
    0,                   /* tp_weaklistoffset */
    PyObject_SelfIter,   /* tp_iter */
    0,                   /* tp_iternext */
    writer_methods,             /* tp_methods */
    0,                          /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Writer_init,      /* tp_init */
    0,                         /* tp_alloc */
    Writer_new,                 /* tp_new */
};

// if the Extension.name is xxx then module name below should be also "xxx",
// PyMODINIT_FUNC must be initxxx.
PyMODINIT_FUNC initlist_file_py() {
  // C++ global constructors are not called when we are a shared object
  util::compressors::internal::RegisterLz4Compression();
  util::compressors::internal::RegisterZlibCompression();

 if (PyType_Ready(&ReaderType) < 0)
   return;
 if (PyType_Ready(&WriterType) < 0)
   return;

  PyObject* m = Py_InitModule("list_file_py", ListMethods);
  if (m == NULL)
    return;
  list_exception = PyErr_NewException(list_exception_name, NULL, NULL);
  Py_INCREF(list_exception);
  PyModule_AddObject(m, "Error", list_exception);

  Py_INCREF(&ReaderType);
  PyModule_AddObject(m, "Reader", (PyObject *)&ReaderType);

  Py_INCREF(&WriterType);
  PyModule_AddObject(m, "Writer", (PyObject *)&WriterType);
}
