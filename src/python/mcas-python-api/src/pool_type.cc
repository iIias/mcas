#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#define NO_IMPORT_ARRAY
#define PY_ARRAY_UNIQUE_SYMBOL mcas_ARRAY_API

#include <string_view>
#include <common/logging.h>
#include <common/dump_utils.h>
#include <common/utils.h>
#include <Python.h>
#include <structmember.h>
#include "pool_type.h"

namespace global
{
extern unsigned debug_level;
}

/* size of values created on demand from ADO invocation */
static constexpr unsigned long DEFAULT_ADO_ONDEMAND_VALUE_SIZE = 64;

extern PyTypeObject PoolType;

static PyObject * pool_close(Pool* self);
static PyObject * pool_count(Pool* self);
static PyObject * pool_put(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_get(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_put_direct(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_get_direct(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_invoke_ado(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_invoke_put_ado(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_get_size(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_erase(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_configure(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_find_key(Pool* self, PyObject *args, PyObject *kwds);
static PyObject * pool_get_attribute(Pool* self, PyObject* args, PyObject* kwds);
static PyObject * pool_type(Pool* self);
static PyObject * pool_free_direct_memory(Pool *self, PyObject* args, PyObject* kwds);


static PyObject *
Pool_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  auto self = (Pool *)type->tp_alloc(type, 0);
  assert(self);

  return (PyObject*)self;
}

Pool * Pool_new()
{
  auto pool = (Pool *) PyType_GenericAlloc(&PoolType,1);
  return pool;
}


/** 
 * tp_dealloc: Called when reference count is 0
 * 
 * @param self 
 */
static void
Pool_dealloc(Pool *self)
{
  assert(self);
  assert(self->_mcas);
  assert(self->_pool);
  PLOG("Pool_dealloc (%p)", self);
  
  /* implicitly close pool */
  self->_mcas->close_pool(self->_pool);
  self->_mcas->release_ref();
  
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyMemberDef Pool_members[] = {
                                     {NULL}
};

PyDoc_STRVAR(type_doc,"Pool.type() -> Return type object.");
PyDoc_STRVAR(put_doc,"Pool.put(key,value) -> Write key-value pair to pool.");
PyDoc_STRVAR(put_direct_doc,"Pool.put_direct(key,value) -> Write bytearray value to pool using zero-copy.");
PyDoc_STRVAR(get_doc,"Pool.get(key) -> Read value from pool.");
PyDoc_STRVAR(get_size_doc,"Pool.get_size(key) -> Get size of a value.");
PyDoc_STRVAR(get_direct_doc,"Pool.get_direct(key) -> Read bytearray value from pool using zero-copy.");
PyDoc_STRVAR(invoke_ado_doc,"Pool.invoke_ado(key,msg) -> Send ADO message.");
PyDoc_STRVAR(invoke_put_ado_doc,"Pool.invoke_put_ado(key,msg,value) -> Send ADO message and perform a pre-put.");
PyDoc_STRVAR(close_doc,"Pool.close() -> Forces pool closure. Otherwise close happens on deletion.");
PyDoc_STRVAR(count_doc,"Pool.count() -> Get number of objects in the pool.");
PyDoc_STRVAR(erase_doc,"Pool.erase(key) -> Erase object from the pool.");
PyDoc_STRVAR(configure_doc,"Pool.configure(jsoncmd) -> Configure pool.");
PyDoc_STRVAR(find_key_doc,"Pool.find(expr, [limit]) -> Find keys using expression.");
PyDoc_STRVAR(get_attribute_doc,"Pool.get_attribute(key, attribute_name) -> Attribute value(s).");
PyDoc_STRVAR(free_direct_memory_doc,"Pool.free_direct_memory(val_from_get_direct) -> Release memory allocated by get_direct call.");

static PyMethodDef Pool_methods[] = {
                                     {"type",(PyCFunction) pool_type, METH_NOARGS, type_doc},
                                     {"close",(PyCFunction) pool_close, METH_NOARGS, close_doc},
                                     {"count",(PyCFunction) pool_count, METH_NOARGS, count_doc},
                                     {"put",(PyCFunction) pool_put, METH_VARARGS | METH_KEYWORDS, put_doc},
                                     {"put_direct",(PyCFunction) pool_put_direct, METH_VARARGS | METH_KEYWORDS, put_direct_doc},
                                     {"get",(PyCFunction) pool_get, METH_VARARGS | METH_KEYWORDS, get_doc},
                                     {"get_direct",(PyCFunction) pool_get_direct, METH_VARARGS | METH_KEYWORDS, get_direct_doc},
                                     {"invoke_ado",(PyCFunction) pool_invoke_ado, METH_VARARGS | METH_KEYWORDS, invoke_ado_doc},
                                     {"invoke_put_ado",(PyCFunction) pool_invoke_put_ado, METH_VARARGS | METH_KEYWORDS, invoke_put_ado_doc},
                                     {"get_size",(PyCFunction) pool_get_size, METH_VARARGS | METH_KEYWORDS, get_size_doc},
                                     {"erase",(PyCFunction) pool_erase, METH_VARARGS | METH_KEYWORDS, erase_doc},
                                     {"configure",(PyCFunction) pool_configure, METH_VARARGS | METH_KEYWORDS, configure_doc},
                                     {"find_key",(PyCFunction) pool_find_key, METH_VARARGS | METH_KEYWORDS, find_key_doc},
                                     {"get_attribute",(PyCFunction) pool_get_attribute, METH_VARARGS | METH_KEYWORDS, get_attribute_doc},
                                     {"free_direct_memory", (PyCFunction) pool_free_direct_memory, METH_VARARGS | METH_KEYWORDS, free_direct_memory_doc},
                                     {NULL}
};



PyTypeObject PoolType = {
                         PyVarObject_HEAD_INIT(NULL, 0)
                         "mcas.Pool",           /* tp_name */
                         sizeof(Pool)   ,      /* tp_basicsize */
                         0,                       /* tp_itemsize */
                         (destructor) Pool_dealloc,      /* tp_dealloc */
                         0,                       /* tp_print */
                         0,                       /* tp_getattr */
                         0,                       /* tp_setattr */
                         0,                       /* tp_reserved */
                         0,                       /* tp_repr */
                         0,                       /* tp_as_number */
                         0,                       /* tp_as_sequence */
                         0,                       /* tp_as_mapping */
                         0,                       /* tp_hash */
                         0,                       /* tp_call */
                         0,                       /* tp_str */
                         0,                       /* tp_getattro */
                         0,                       /* tp_setattro */
                         0,                       /* tp_as_buffer */
                         Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
                         "Pool",              /* tp_doc */
                         0,                       /* tp_traverse */
                         0,                       /* tp_clear */
                         0,                       /* tp_richcompare */
                         0,                       /* tp_weaklistoffset */
                         0,                       /* tp_iter */
                         0,                       /* tp_iternext */
                         Pool_methods,         /* tp_methods */
                         Pool_members,         /* tp_members */
                         0,                       /* tp_getset */
                         0,                       /* tp_base */
                         0,                       /* tp_dict */
                         0,                       /* tp_descr_get */
                         0,                       /* tp_descr_set */
                         0,                       /* tp_dictoffset */
                         0, //(initproc)Pool_init,  /* tp_init */
                         0,            /* tp_alloc */
                         Pool_new,             /* tp_new */
                         0, /* tp_free */
};

  

static PyObject * pool_put(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"key",
                                 "value",
                                 "no_stomp",
                                 NULL};

  const char * key = nullptr;
  PyObject * value = nullptr;
  int do_not_stomp = 0;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "sO|p",
                                    const_cast<char**>(kwlist),
                                    &key,
                                    &value,
                                    &do_not_stomp)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  assert(self->_kvstore);
  assert(self->_pool != IKVStore::POOL_ERROR);

  if(self->_pool == 0) {
    PyErr_SetString(PyExc_RuntimeError,"already closed");
    return NULL;
  }

  void * p = nullptr;
  size_t p_len = 0;
  if(PyByteArray_Check(value)) {
    p = PyByteArray_AsString(value);
    p_len = PyByteArray_Size(value);
  }
  else if(PyUnicode_Check(value)) {
    p = PyUnicode_DATA(value);
    p_len = PyUnicode_GET_SIZE(value);
  }
  else {
    PyErr_SetString(PyExc_RuntimeError,"bad value parameter");
    return NULL;
  }

  unsigned int flags = 0;
  auto hr = self->_mcas->put(self->_pool,
                             key,
                             p,
                             p_len,
                             flags);
                                    
  if(hr != S_OK) {
    std::stringstream ss;
    ss << "pool.put failed [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());
    return NULL;
  }
                                    
  Py_INCREF(self);
  return (PyObject *) self;
}


static PyObject * pool_put_direct(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"key",
                                 "value",
                                 NULL};

  const char * key = nullptr;
  PyObject * value = nullptr;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "sO|",
                                    const_cast<char**>(kwlist),
                                    &key,
                                    &value)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  void * p = nullptr;
  size_t p_len = 0;
  
  if(PyByteArray_Check(value)) {
    p = PyByteArray_AsString(value);
    p_len = PyByteArray_Size(value);
  }
  else if(PyMemoryView_Check(value)) {
    auto buffer = PyMemoryView_GET_BUFFER(value);
    p = buffer->buf;
    p_len = buffer->len;
  }
  // else if(PyArray_Check(value)) {
  //   /* see https://docs.scipy.org/doc/numpy-1.13.0/reference/c-api.array.html */
  //   auto array_obj = reinterpret_cast<PyArrayObject *>(value);
  //   p = PyArray_DATA(array_obj);
  //   p_len = PyArray_NBYTES(array_obj);
  // }
  else {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  unsigned int flags = 0;
  status_t hr;
  component::IKVStore::memory_handle_t handle = self->_mcas->register_direct_memory(p, p_len);

  if(handle == nullptr) {
    PyErr_SetString(PyExc_RuntimeError,"RDMA memory registration failed");
    return NULL;
  }

  hr = self->_mcas->put_direct(self->_pool,
                               key,
                               p,
                               p_len,
                               handle,
                               flags);
                                    
  if(hr != S_OK) {
    std::stringstream ss;
    ss << "pool.put_direct failed [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());
    return NULL;
  }

  /* unregister memory */
  self->_mcas->unregister_direct_memory(handle);
  
  Py_INCREF(self);
  return (PyObject *) self;
}


static PyObject * pool_get(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"key",
                                 NULL};

  const char * key = nullptr;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "s",
                                    const_cast<char**>(kwlist),
                                    &key)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  assert(self->_kvstore);
  assert(self->_pool != IKVStore::POOL_ERROR);

  if(self->_pool == 0) {
    PyErr_SetString(PyExc_RuntimeError,"already closed");
    return NULL;
  }

  void * out_p = nullptr;
  size_t out_p_len = 0;
  auto hr = self->_mcas->get(self->_pool,
                             key,
                             out_p,
                             out_p_len);

  if(hr == component::IKVStore::E_KEY_NOT_FOUND) {
    Py_RETURN_NONE;
  }
  else if(hr != S_OK || out_p == nullptr) {
    std::stringstream ss;
    ss << "pool.get failed [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());
    return NULL;
  }

  /* copy value string */
  auto result = PyUnicode_FromStringAndSize(static_cast<const char*>(out_p), out_p_len);
  self->_mcas->free_memory(out_p);

  return result;
}


static PyObject * pool_get_direct(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"key",
                                 NULL};

  const char * key = nullptr;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "s",
                                    const_cast<char**>(kwlist),
                                    &key)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  assert(self->_pool);
    
  std::vector<uint64_t> v;
  std::string k(key);
  
  auto hr = self->_mcas->get_attribute(self->_pool,
                                       component::IKVStore::Attribute::VALUE_LEN,
                                       v,
                                       &k);

  if(hr != S_OK || v.size() != 1) {
    std::stringstream ss;
    ss << "pool.get_direct failed [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());
    return NULL;
  }
  
  /* now we have the buffer size, we can allocate accordingly */
  size_t p_len = v[0];
  char * ptr = static_cast<char*>(::aligned_alloc(PAGE_SIZE, p_len));

  if(global::debug_level > 0)
    PLOG("%s allocated %lu at %p", __func__, p_len, ptr);

  PyObject * result = PyMemoryView_FromMemory(ptr, p_len, PyBUF_WRITE); //PyBytes_FromStringAndSize(NULL, p_len);

  /* register memory */
  component::IKVStore::memory_handle_t handle = self->_mcas->register_direct_memory(ptr, round_up_page(p_len));

  if(handle == nullptr) {
    PyErr_SetString(PyExc_RuntimeError,"RDMA memory registration failed");
    return NULL;
  }

  /* now perform get_direct */
  hr = self->_mcas->get_direct(self->_pool, k, ptr, p_len, handle);
  if(hr != S_OK) {
    std::stringstream ss;
    ss << "pool.get_direct failed [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());
    return NULL;
  }

  self->_mcas->unregister_direct_memory(handle);
  
  return result;
}


static PyObject * pool_invoke_ado(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"key",
                                 "command",
                                 "ondemand_size",
                                 "flags",
                                 NULL};

  const char * key = nullptr;
  PyObject * command = nullptr;
  unsigned long ondemand_size = DEFAULT_ADO_ONDEMAND_VALUE_SIZE;
  unsigned long flags = 0;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "sO|kk",
                                    const_cast<char**>(kwlist),
                                    &key,
                                    &command,
                                    &ondemand_size,
                                    &flags)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments to invoke_ado");
    return NULL;
  }

  /* command can be a byte array or a unicode string */
  void * cmd = nullptr;
  size_t cmd_len = 0;
  if(PyByteArray_Check(command)) {
    cmd = PyByteArray_AsString(command);
    cmd_len = PyByteArray_Size(command);
  }
  else if(PyUnicode_Check(command)) {
    cmd = PyUnicode_DATA(command);
    cmd_len = PyUnicode_GET_SIZE(command);
  }
  else {
    PyErr_SetString(PyExc_RuntimeError,"bad value parameter");
    return NULL;
  }

  assert(self->_mcas);
  assert(self->_pool);
  assert(cmd_len > 0);
  
  std::vector<component::IMCAS::ADO_response> response;

  status_t hr = self->_mcas->invoke_ado(self->_pool,
                                        key,
                                        cmd,
                                        cmd_len,
                                        flags,
                                        response,
                                        ondemand_size);

  if(hr != S_OK) {
    std::stringstream ss;
    ss << "invoke_ado failed (" << hr << ")";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());
    return NULL;
  }

  if((response.size() > 0) &&
     (response[0].data_len() > 0) &&
     (response[0].data())) {

    //hexdump(response[0].data(),response[0].data_len());    
    return PyByteArray_FromStringAndSize((const char *) response[0].data(), response[0].data_len());
  }
  else {
    Py_RETURN_NONE;
  }
}


static PyObject * pool_invoke_put_ado(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"key",
                                 "command",
                                 "value",
                                 NULL};

  const char * key = nullptr;
  PyObject * value = nullptr;
  PyObject * command = nullptr;
  unsigned long flags = 0;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "sOO",
                                    const_cast<char**>(kwlist),
                                    &key,
                                    &value,
                                    &command,
                                    &flags)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  /* command can be a byte array or a unicode string */
  void * cmd = nullptr;
  size_t cmd_len = 0;
  if(PyByteArray_Check(command)) {
    cmd = PyByteArray_AsString(command);
    cmd_len = PyByteArray_Size(command);
  }
  else if(PyUnicode_Check(command)) {
    cmd = PyUnicode_DATA(command);
    cmd_len = PyUnicode_GET_SIZE(command);
  }
  else {
    PyErr_SetString(PyExc_RuntimeError,"bad value parameter");
    return NULL;
  }

  /* value can be a byte array or a unicode string */
  void * p = nullptr;
  size_t p_len = 0;
  if(PyByteArray_Check(value)) {
    p = PyByteArray_AsString(value);
    p_len = PyByteArray_Size(value);
  }
  else if(PyUnicode_Check(value)) {
    p = PyUnicode_DATA(value);
    p_len = PyUnicode_GET_SIZE(value);
  }
  else {
    PyErr_SetString(PyExc_RuntimeError,"bad value parameter");
    return NULL;
  }

  assert(self->_mcas);
  assert(self->_pool);

  std::vector<component::IMCAS::ADO_response> response;

  status_t hr = self->_mcas->invoke_put_ado(self->_pool,
                                            key,
                                            cmd,
                                            cmd_len,
                                            p,
                                            p_len,
                                            0, // root len
                                            flags & component::IMCAS::ADO_FLAG_CREATE_ON_DEMAND, // flags
                                            response);

  if(hr != S_OK) {
    std::stringstream ss;
    ss << "invoke_ado failed (" << hr << ")";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());
    return NULL;
  }

  if((response.size() > 0) &&
     (response[0].data_len()) > 0 &&
     (response[0].data())) {
    return PyByteArray_FromStringAndSize((const char *) response[0].data(), response[0].data_len());
  }
  else {
    Py_RETURN_NONE;
  }
}



static PyObject * pool_get_size(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"key",
                                 NULL};

  const char * key = nullptr;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "s",
                                    const_cast<char**>(kwlist),
                                    &key)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  assert(self->_pool);
    
  std::vector<uint64_t> v;
  std::string k(key);
  
  auto hr = self->_mcas->get_attribute(self->_pool,
                                       component::IKVStore::Attribute::VALUE_LEN,
                                       v,
                                       &k);

  if(hr != S_OK || v.size() != 1) {
    std::stringstream ss;
    ss << "pool.get_size failed [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());
    return NULL;
  }
  
  return PyLong_FromSize_t(v[0]);
}

static PyObject * pool_type(Pool* self)
{
  return PyUnicode_FromString(PoolType.tp_name);
}

static PyObject * pool_close(Pool* self)
{
  assert(self->_mcas);
  assert(self->_pool != IKVStore::POOL_ERROR);

  if(self->_pool == 0) {
    PyErr_SetString(PyExc_RuntimeError,"mcas.Pool.close failed. Already closed.");
    return NULL;
  }

  status_t hr = self->_mcas->close_pool(self->_pool);
  self->_pool = 0;

  if(hr != S_OK) {
    std::stringstream ss;
    ss << "pool.close failed [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject * pool_count(Pool* self)
{
  assert(self->_mcas);
  assert(self->_pool != IKVStore::POOL_ERROR);

  if(self->_pool == 0) {
    PyErr_SetString(PyExc_RuntimeError,"mcas.Pool.count failed. Already closed.");
    return NULL;
  }

  return PyLong_FromSize_t(self->_mcas->count(self->_pool));
}

static PyObject * pool_erase(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"key",
                                 NULL};

  const char * key = nullptr;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "s",
                                    const_cast<char**>(kwlist),
                                    &key)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  assert(self->_pool);
    
  std::string k(key);
  
  auto hr = self->_mcas->erase(self->_pool, k);

  if(hr != S_OK) {
    std::stringstream ss;
    ss << "pool.erase [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());    
    return NULL;
  }

  Py_RETURN_TRUE;
}


static PyObject * pool_configure(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"command",
                                 NULL};

  const char * command = nullptr;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "s",
                                    const_cast<char**>(kwlist),
                                    &command)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  assert(self->_pool);
    
  const std::string cmd(command);
  
  auto hr = self->_mcas->configure_pool(self->_pool, cmd);

  if(hr != S_OK) {
    std::stringstream ss;
    ss << "pool.configure [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());    
    return NULL;
  }

  Py_RETURN_TRUE;
}



static PyObject * pool_find_key(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"expr",
                                 "offset",
                                 NULL};

  const char * expr_param = nullptr;
  int offset_param = 0;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "s|i",
                                    const_cast<char**>(kwlist),
                                    &expr_param,
                                    &offset_param)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  assert(self->_pool);
    
  const std::string expr(expr_param);

  std::string out_key;
  offset_t out_pos = 0;
  auto hr = self->_mcas->find(self->_pool,
                              expr,                              
                              offset_param,
                              out_pos,
                              out_key);

  if(hr == S_OK) {
    auto tuple = PyTuple_New(2);
    PyTuple_SetItem(tuple, 0, PyUnicode_FromString(out_key.c_str()));
    PyTuple_SetItem(tuple, 1, PyLong_FromUnsignedLong(out_pos));
    return tuple;
  }
  else if(hr == E_FAIL) {
    auto tuple = PyTuple_New(2);
    PyTuple_SetItem(tuple, 0, Py_None);
    PyTuple_SetItem(tuple, 1, Py_None);
    return tuple;
  }
  else {
    std::stringstream ss;
    ss << "pool.find [status:" << hr << "]";
    PyErr_SetString(PyExc_RuntimeError,ss.str().c_str());    
    return NULL;
  }

  return NULL;
}


static PyObject * pool_get_attribute(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"key",
                                 "attr",
                                 NULL};

  const char * key = nullptr;
  const char * attrstr = nullptr;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "ss",
                                    const_cast<char**>(kwlist),
                                    &key,
                                    &attrstr)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  assert(self->_pool);
    
  std::vector<uint64_t> v;
  std::string k(key);
  std::string attr(attrstr);
  component::IKVStore::Attribute attribute;
  
  if(attr == "length")
    attribute = component::IKVStore::Attribute::VALUE_LEN;
  else if(attr == "crc32")
    attribute = component::IKVStore::Attribute::CRC32;
  else {
    PyErr_SetString(PyExc_RuntimeError,"bad attribute name");
    return NULL;    
  }
  
  auto hr = self->_mcas->get_attribute(self->_pool,
                                       attribute,
                                       v,
                                       &k);

  if(hr != S_OK) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;    
  }
  
  if(v.size() == 1) {
    return PyLong_FromUnsignedLong(v[0]);
  }
  else {
    auto list = PyList_New(v.size());
    for(auto value: v)
      PyList_Append(list, PyLong_FromUnsignedLong(value));
    return list;
  }
}


static PyObject * pool_free_direct_memory(Pool* self, PyObject *args, PyObject *kwds)
{
  static const char *kwlist[] = {"data",
                                 NULL};

  PyObject * memory_view = nullptr;
  
  if (! PyArg_ParseTupleAndKeywords(args,
                                    kwds,
                                    "O",
                                    const_cast<char**>(kwlist),
                                    &memory_view)) {
    PyErr_SetString(PyExc_RuntimeError,"bad arguments");
    return NULL;
  }

  if (! PyMemoryView_Check(memory_view)) {
    PyErr_SetString(PyExc_RuntimeError,"argument should be <memoryview> type");
    return NULL;
  }

  Py_buffer * buffer = PyMemoryView_GET_BUFFER(memory_view);

  /* release memory */
  ::free(buffer->buf);

  if(global::debug_level > 0)
    PLOG("released memory (%p)", buffer->buf);
  
  Py_RETURN_TRUE;
}
