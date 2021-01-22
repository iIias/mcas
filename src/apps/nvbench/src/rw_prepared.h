#ifndef NVBENCH_RW_PREPARED_H
#define NVBENCH_RW_PREPARED_H

#include "external_count.h"
#include "geometry.h"
#include "mem_test.h"
#include "persist.h"

#include "wrapper_cycles.h"

#include <cstddef>
#include <tuple>
#include <vector>

/* TESTS (generic, small access) */

static inline std::tuple<cpu_time_t, std::size_t> random_8byte_read_prepared(const mem_test<uint64_t> &mt, std::size_t collection_size_)
{
  const auto m = mt.data();
  const cpu_time_t start = rdtsc();
  if ( collection_size_ )
  {
    auto pos = m[0];
    while ( pos != 0 ) {
      pos = m[pos];
    }
    external_count = pos;
  }
  return std::tuple<cpu_time_t, std::size_t>(rdtsc() - start, collection_size_);
}

static inline cpu_time_t random_8byte_write_prepared(const mem_test<uint64_t> &mt, const std::vector<std::size_t> &positions_)
{
  const auto m = mt.data();

  const cpu_time_t start = rdtsc();
  for(auto pos : positions_) {
    m[pos] = pos;
  }
  return rdtsc() - start;
}

static inline cpu_time_t random_8byte_write_persist_prepared(const mem_test<uint64_t> &mt, const std::vector<std::size_t> &positions_)
{
  const auto m = mt.data();

  const cpu_time_t start = rdtsc();
  for(auto pos : positions_) {
    m[pos] = pos;
    persist(m[pos]);
  }
  return rdtsc() - start;
}

#endif
