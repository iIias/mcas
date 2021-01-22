#ifndef __PMEM_XMS_HELPER__
#define __PMEM_XMS_HELPER__

#include "pmem_access.h"
#include "wrapper_utils.h"

#include <sys/mman.h>

#include <cassert>
#include <cstring>

class Pmem_xms_file
{
public:
  Pmem_xms_file(const char *fn)
    : _fd(open(fn, O_RDWR))
  {
    if ( _fd == -1 ) {
      auto e = errno;
      throw Constructor_exception("open failed because %s", strerror(e));
    }
  }
  ~Pmem_xms_file() {
    close(_fd);
  }

  void *alloc(std::size_t n_bytes, std::size_t addr) {
    assert(check_aligned(n_bytes, PAGE_SIZE));

    auto base = mmap(NULL,
                 n_bytes,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 _fd,
                 addr);

    if(base == MAP_FAILED) {
      auto e = errno;
      throw Constructor_exception("mmap failed because %s", strerror(e));
    }
    return base;
  }
private:
  int    _fd;
};

class Pmem_xms
  : private Pmem_xms_file
  , public Pmem_access
{
  const addr_t PMEM_ADDR = 0x0000002080000000ULL; /* skylake test server */
public:
 Pmem_xms(size_t n_bytes, size_t offset)
   : Pmem_xms_file{"/dev/xms"}
   , Pmem_access(alloc(n_bytes, PMEM_ADDR + offset), n_bytes, "/dev/xms") {
  }

  void flush(const void *first, const void *last) const override
  {
    auto sz = static_cast<const char *>(last) - static_cast<const char *>(first);
    if ( 0 < sz )
    {
      clflush_area(const_cast<void *>(first), sz);
      wmb();
    }
  }

  ~Pmem_xms() {
    munmap(p_base(), size());
  }

  virtual const char *type() const override { return "NVDIMM"; }
};


#endif //__PMEM_XMS_HELPER__
