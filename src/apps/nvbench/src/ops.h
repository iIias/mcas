#ifndef NVBENCH_OPS_H
#define NVBENCH_OPS_H

#include "persist.h"

#include <cstring>

void *memcpy_and_flush(void *dst, const void *src, std::size_t size);
void *memset_and_flush(void *dst, int src, std::size_t size);

/*
 * Types of persistence. We have three types:
 *  persist: mem* functions with persistence through pmem
 *  nodrain: mem* functions through pmem, but without persistence
 *  std: standard mem* functions, not the pmem versions
 *
 * Distinguishing among persist, nodrain, and std at compile time may be overkill,
 * but we intend to avoid the extra dereference in the inner loop. May be overkill
 * for mem operations, but could be if extended to simple integer operations.
 */

enum class persistence { PERSIST, NODRAIN, FLUSH, STD, };

template <persistence> class OPS;

template <>
  class OPS<persistence::PERSIST> {
  public:
    static constexpr auto cpy = pmem_memcpy_persist;
    static constexpr auto set = pmem_memset_persist;
    static constexpr char tag[] = "persist";
  };

template <>
    class OPS<persistence::NODRAIN> {
  public:
    static constexpr auto cpy = pmem_memcpy_nodrain;
    static constexpr auto set = pmem_memset_nodrain;
    static constexpr char tag[] = "nodrain";
  };

template <>
  class OPS<persistence::STD> {
  public:
    static constexpr auto cpy = std::memcpy;
    static constexpr auto set = std::memset;
    static constexpr char tag[] = "std";
  };

template <>
  class OPS<persistence::FLUSH> {
  public:
    static constexpr auto cpy = memcpy_and_flush;
    static constexpr auto set = memset_and_flush;
    static constexpr char tag[] = "flush";
  };

#endif
