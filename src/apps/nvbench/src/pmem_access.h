#ifndef __PMEM_ACCESS_H__
#define __PMEM_ACCESS_H__

#include "wrapper_logging.h"

#include <cstddef>
#include <numaif.h>

#include <string>

class Pmem_access
{
public:
  Pmem_access(void *pbase_, size_t size_, std::string id_)
    : _pbase{pbase_}
    , _size{size_}
    , _id{id_}
  {}
  Pmem_access(void *pbase_, size_t size_, int node_)
    : _pbase{pbase_}
    , _size{size_}
    , _id{std::string{"numa "} + std::to_string(node_ < 0 ? node_of(_pbase) : node_)}
  {}
  Pmem_access(const Pmem_access &) = default; /* ERROR: _pbase is a shared bare pointer */
  Pmem_access &operator=(const Pmem_access &) = delete;
  virtual ~Pmem_access() {}

  void * p_base() const { return _pbase; }
  void * p_end() const { return static_cast<char *>(_pbase) + size(); }
  std::string id() const { return _id; }

  size_t size() const { return _size; }

  void flush() const
  {
    flush(p_base(), p_end());
  }
  virtual void flush(const void *first, const void *last) const = 0;
  virtual const char *type() const = 0;
  /* numa node, or -1 if not certain */
  static int node_of(void *p)
  {
    int status[1];
    auto rc = move_pages(0, 1, &p, nullptr, status, 0);
    return rc == 0 ? status[0] < 0 ? -1 : status[0] : -1;
  }

private:

  void * _pbase;
  size_t _size;
  std::string _id;
};

#endif
