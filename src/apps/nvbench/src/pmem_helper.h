#ifndef __PMEM_HELPER_H__
#define __PMEM_HELPER_H__

#include "pmem_access.h"

#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstring>
#include <cstddef>
#include <string>
#include <libpmemobj.h>
#include <libpmem.h>
#include "wrapper_exceptions.h"
#include "wrapper_utils.h"

class Pmem_object_pool
{
public:
  Pmem_object_pool(const char *fn, size_t nb_size)
    : _pop(alloc(fn, nb_size))
  {}
  Pmem_object_pool(const Pmem_object_pool &) = delete;
  Pmem_object_pool &operator=(const Pmem_object_pool &) = delete;
  ~Pmem_object_pool() {
    pmemobj_close(_pop);
  }

  PMEMoid get_oid(std::size_t nb_size_) const {
    auto r = pmemobj_root(_pop, nb_size_ + KB(4));
    if ( 0 == std::memcmp(&OID_NULL, &r, sizeof OID_NULL) )
    {
      const auto e = errno;
      throw General_exception("pmemobj_root size=%zu (maximum allowed size %zu) failed because %s", nb_size_, PMEMOBJ_MAX_ALLOC_SIZE, strerror(e));
    }
    return r;
  }

private:
  PMEMobjpool *_pop;
  PMEMobjpool *alloc(const std::string filename, size_t nb_size) {
    const char *fn = filename.c_str();
    if ( nb_size < PMEMOBJ_MIN_POOL ) {
      throw Constructor_exception("Pmem_base %s refused because size %zu less than minuimum %zu", fn, nb_size, PMEMOBJ_MIN_POOL);
    }
    assert(nb_size >= PMEMOBJ_MIN_POOL);

    PMEMobjpool *pop = pmemobj_open(fn, "mb_0");

    auto e_o = errno;
    if ( pop == nullptr ) {
      pop = pmemobj_create(fn, "mb_0", nb_size * 2, 0666);
    }
    auto e_c = errno;

    if ( pop == nullptr ) {
        throw Constructor_exception("pmemobj %s size=%lu open failed because %s and create failed because %s", fn, nb_size, strerror(e_o), strerror(e_c));
    }
    return pop;
  }
};

class Pmem_oid
{
public:
  Pmem_oid(PMEMoid oid_)
    : _oid{oid_}
  {}
  void *get_base(const char *fn)
  {
    auto pbase = reinterpret_cast<void*>(round_up_page(reinterpret_cast<addr_t>(pmemobj_direct(_oid))));
    if ( ! pbase )
    {
      throw Constructor_exception("pmemobj_direct %s addressing failed layout %" PRIu64 " off %" PRIu64 " because %s", fn, _oid.pool_uuid_lo, _oid.off, strerror(errno));
    }
    return pbase;
  }
private:
  PMEMoid _oid;
};

class Pmem_base
  : private Pmem_object_pool
  , private Pmem_oid
  , public Pmem_access
{
public:
  Pmem_base(const std::string filename, size_t nb_size)
    : Pmem_object_pool{filename.c_str(), nb_size}
    , Pmem_oid{get_oid(nb_size)}
    , Pmem_access{get_base(filename.c_str()), nb_size, filename}
  {
    PLOG("pbase: %p", p_base());
    pmem_memset_persist(p_base(), 0, MB(4));
  }

  void flush(const void *first, const void *last) const override
  {
    auto sz = static_cast<const char *>(last) - static_cast<const char *>(first);
    if ( 0 < sz )
    {
      pmem_flush(first, sz);
    }
  }

  virtual const char *type() const override { return "NVDIMM"; }
};

#endif
