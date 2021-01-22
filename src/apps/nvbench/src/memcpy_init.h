#ifndef NBENCH_MEMCPY_INIT_H
#define NBENCH_MEMCPY_INIT_H

#include "geometry.h"
#include "mem_test.h"
#include "rw_pos.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

static inline std::size_t memcpy_init(const mem_test<uint64_t> &pm_src, const std::vector<std::uint64_t> &positions, std::size_t sz_)
{
	/* From the random list, create a cycle */
	const auto src = pm_src.data();
	/*
	 * "stride" is measured in elements of the pm_src array, i.e. uint64_t elements.
	 * It is used to touch all cache lines in the element.
	 */
	const auto stride = std::max(sz_,geometry::slot_size())/sizeof *src;
#if 0
	if ( positions.size() != 0 )
	{
		auto pos_input = positions;
		std::sort(pos_input.begin(), pos_input.end());
		PLOG("%s:%zu position input from %zu to %zu count %zu stride %zu to fill area size %zu"
			, __func__, sz_, pos_input.front(), pos_input.back(), positions.size(), stride, pm_src.size()
		);
	}
#endif
	std::vector<std::size_t> pos_vec;

	if ( ! positions.empty() )
	{
		auto i = positions.begin();
		/* In each position, store the next position to access, creating a cycle of all positions */
		while ( i+1 != positions.end() )
		{
			const auto pos = *i;
			++i;
			auto next = *i;

			rw_pos::write_pos(&src[pos], &src[pos+stride], next);
			if ( pm_src.geo().sanity_check() ) { pos_vec.emplace_back(pos); }
		}
		const auto pos = *i;
		auto next = positions.front();
		rw_pos::write_pos(&src[pos], &src[pos+stride], next);
		if ( pm_src.geo().sanity_check() ) { pos_vec.emplace_back(pos); }
	}
	if ( pos_vec.size() != 0 )
	{
		std::sort(pos_vec.begin(), pos_vec.end());
		PLOG("%s:%zu region at %p from %zu to %zu count %zu", __func__, sz_, static_cast<void*>(src), pos_vec.front(), pos_vec.back(), pos_vec.size());
	}
	pm_src.flush();
	return positions.size();
}

#endif
