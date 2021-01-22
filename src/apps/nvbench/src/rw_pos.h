#ifndef NVBENCH_RW_POS_H
#define NVBENCH_RW_POS_H

#include "geometry.h"

#include <cassert>
#include <cstdint>

struct rw_pos_simple
{
	static void write_pos(std::uint64_t *first, std::uint64_t *, uint64_t pos)
	{
		*first = pos;
	}

	static uint64_t read_pos(const std::uint64_t *first, const std::uint64_t *)
	{
		return *first;
	}
};

/* To ensure(?) that the memcpy has moved all words, set and read both the first
 * and last word of a memcpy dst operand
 */
struct rw_pos_front_back
{
	static void write_pos(std::uint64_t *first, std::uint64_t *last, uint64_t pos)
	{
		*first = pos;
		*(last-1) = pos;
	}

	static uint64_t read_pos(const std::uint64_t *first, const std::uint64_t *last)
	{
		const auto pos1 = *first;
		const auto pos2 = *(last-1);
		assert(pos1 == pos2);
		return (pos1 + pos2) / 2;
	}
};

/* To really ensure that the memcpy has moved all words, set and read all cache lines */
struct rw_pos_all
{
	/* Use only one word per cache line */

	static void write_pos(std::uint64_t *first, std::uint64_t *last, uint64_t pos)
	{
		*first = pos;
		first += geometry::uint64_per_slot();
		for ( ; first < last; first += geometry::uint64_per_slot() )
		{
			*first = 0;
		}
	}

	static uint64_t read_pos(const std::uint64_t *first, const std::uint64_t *last)
	{
		uint64_t pos = 0;
		for ( ; first < last; first += geometry::uint64_per_slot() )
		{
			pos += *first;
		}
		return pos;
	}
};

using rw_pos = rw_pos_all;

#endif
