#ifndef NVBENCH_MEM_RUN_H
#define NVBENCH_MEM_RUN_H

#include "cpu_measured.h"
#include "external_count.h"
#include "mem_test.h"
#include "memcpy_init.h"
#include "ops.h"
#include "pattern.h"
#include "rw_pos.h"

#include "wrapper_cpu.h"
#include "wrapper_cycles.h"
#include "join.h"
#include "wrapper_logging.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

static inline std::tuple<cpu_time_t, std::size_t> memget_run(std::uint64_t *dst, std::uint64_t *src, std::size_t item_size_, std::size_t collection_size_, bool sanity_check_)
{
	std::vector<std::uint64_t> log;
	std::set<std::uint64_t> visited;

	std::size_t bytes = 0;
	/*
	 * "stride" is measured in elements of the pm_src array, i.e. uint64_t elements.
	 * It is used to touch all cache lines in the element.
	 */
	const auto stride = std::max(item_size_,geometry::slot_size())/sizeof *src;

	const cpu_time_t start = rdtsc();
	if ( collection_size_ )
	{
		std::size_t pos = 0;
		auto next = rw_pos::read_pos(&src[pos], &src[pos+stride]);
		if ( sanity_check_ )
		{
			if ( visited.count(next) )
			{
				PLOG("Revisited %zu after %zu entries", next, log.size());
				PLOG("%s", Common::join(" ", log.begin(), log.end()).c_str());
				throw std::logic_error{"Bad ccycle in randomisze read list"};
			}
			visited.insert(next);
			log.emplace_back(next);
		}
		if ( item_size_ * collection_size_ < next )
		{
			PLOG("%s %zu element at %zu : %zu exceeds bound %zu", __func__, item_size_, pos, next, item_size_ * collection_size_);
		}
		pos = next;
		std::memcpy(dst, &src[pos], item_size_);
		bytes += item_size_;
		while ( pos != 0 ) {
			next = rw_pos::read_pos(&src[pos], &src[pos+stride]);
			if ( sanity_check_ )
			{
				if ( visited.count(next) )
				{
					PLOG("Revisited %zu after %zu entries", next, log.size());
					PLOG("%s", Common::join(" ", log.begin(), log.end()).c_str());
					throw std::logic_error{"Bad ccycle in randomisze read list"};
				}
				visited.insert(next);
				log.emplace_back(next);
			}
			if ( item_size_ * collection_size_ < next )
			{
				PLOG("%s %zu element at %zu : %zu exceeds bound %zu", __func__, item_size_, pos, next, item_size_ * collection_size_);
			}
			pos = next;
			std::memcpy(dst, &src[pos], item_size_);
			bytes += item_size_;
		}
		external_count = pos;
	}

	return std::tuple<cpu_time_t, std::size_t>(rdtsc() - start, collection_size_);
}

template <enum persistence P>
	std::tuple<cpu_time_t, std::size_t> memcpy_run(std::uint64_t *dst, std::uint64_t *src, const std::vector<std::uint64_t> &positions, std::size_t item_size_, bool sanity_check_)
	{
		using attrs = OPS<P>; /* persistence attributes */

		std::vector<std::uint64_t> log;
		std::set<std::uint64_t> visited;

		const cpu_time_t start = rdtsc();
		auto pos = src[0];
		if ( sanity_check_ )
		{
			if ( visited.count(pos) )
			{
				PLOG("Revisited %zu after %zu entries", pos, log.size());
				PLOG("%s", Common::join(" ", log.begin(), log.end()).c_str());
				throw std::logic_error{"Bad ccycle in randomisze read list"};
			}
			visited.insert(pos);
			log.emplace_back(pos);
		}
		attrs::cpy(&dst[pos], &src[pos], item_size_);
		while ( pos != 0 ) {
			pos = src[pos];
			attrs::cpy(&dst[pos], &src[pos], item_size_);
			if ( sanity_check_ )
			{
				if ( visited.count(pos) )
				{
					PLOG("Revisited %zu after %zu entries", pos, log.size());
					PLOG("%s", Common::join(" ", log.begin(), log.end()).c_str());
					throw std::logic_error{"Bad ccycle in randomisze read list"};
				}
				visited.insert(pos);
				log.emplace_back(pos);
			}
		}
		external_count = src[pos];
		const cpu_time_t end = rdtsc();

		return std::tuple<cpu_time_t, std::size_t>{end - start, positions.size()};
	}

template <enum persistence P>
	void memcpy_test(std::shared_ptr<Pmem_access> m0, std::shared_ptr<Pmem_access> m1, const cpu_measured &cpu_, const pattern &pat_, std::size_t sz_)
	{
		using attrs = OPS<P>; /* persistenc attributes */

		const mem_test<uint64_t> pm_dst{m0, pat_.geo()};
		const mem_test<uint64_t> pm_src{m1, pat_.geo()};
		const auto positions = pat_.generate(sz_);
		memcpy_init(pm_src, positions, sz_);
		const auto r = memcpy_run<P>(pm_dst.data(), pm_src.data(), positions, sz_, pat_.sanity_check());
		PINF("%s_%s_%zubyte_memcpy_%s: per op: %f nsec", pm_dst.type(), pat_.name(), sz_, attrs::tag, cpu_.tsc_to_nsec(std::get<0>(r))/double(std::get<1>(r)));
	}

template <enum persistence P>
	std::tuple<cpu_time_t, std::size_t> memset_run(std::uint64_t *dst, const std::vector<std::uint64_t> &positions, std::size_t sz_, int fillchar)
	{
		using attrs = OPS<P>; /* persistence attributes */

		const cpu_time_t start = rdtsc();
		for(auto pos : positions) {
			attrs::set(&dst[pos], fillchar, sz_);
		}
		const cpu_time_t end = rdtsc();

		return std::tuple<cpu_time_t, std::size_t>{end - start, positions.size()};
	}

#endif
