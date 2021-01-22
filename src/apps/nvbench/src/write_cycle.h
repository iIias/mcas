#ifndef NVBENCH_WRITE_CYCLE_H
#define NVBENCH_WRITE_CYCLE_H

#include "memcpy_init.h"
#include "pattern.h"

static inline std::size_t write_cycle(const mem_test<std::uint64_t> &pm_src, const std::size_t collection_sz_)
{
	const auto positions = pattern_random{pm_src.geo()}.generate(collection_sz_);
	return memcpy_init(pm_src, positions, sizeof(uint64_t));
}

#endif
