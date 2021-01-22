#ifndef NVBENCH_LOAD_GENERATOR_H
#define NVBENCH_LOAD_GENERATOR_H

#include "memcpy_init.h"
#include "mem_run.h"
#include "mem_test.h"
#include "write_cycle.h"

#include "cpu_mask_iterator.h"
#include "geometry.h"
#include "pattern.h"
#include "rw_prepared.h"
#include "join.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstddef> /* size_t */
#include <memory> /* shared_ptr */
#include <string>
#include <tuple>
#include <system_error>
#include <vector>

class load_generator
{
	mem_test<uint64_t> _pm;
	cpu_mask_t _aff;
	std::size_t _element_size;
	bool _sanity_check;
protected:
	~load_generator() {}
public:
	/* Construtor for any CPU-intensive setup prior to start of test
	 *   mem_: memory against which to run
	 *   cpu_ core to use
	 *   sz_: number of bytes per memset
	 *   g_: geometry (to avoid running past end of memory when sz_ exceeds a cache line)
	 * (there is an additional 4K added to the address space over in the pmem routines, which may
	 * indicate that g_ is not required. Not sure though, as there's no explanation of it.)
	 */
	explicit load_generator(std::shared_ptr<Pmem_access> mem_, int cpu_, geometry_bounded geo_, std::size_t element_size_)
		: _pm{mem_, geo_}
		, _aff{}
		, _element_size{element_size_}
		, _sanity_check(geo_.sanity_check())
	{
		_aff.add_core(cpu_);
	}
	const mem_test<uint64_t> &pm() const { return _pm; }
	bool sanity_check() const { return _sanity_check; }
	void set_affinity() 
	{
		if ( 0 != set_cpu_affinity_mask(_aff) )
		{
			const auto e = errno;
			throw std::system_error{std::error_code{e, std::system_category()}, std::string{"Failed to set affinity "} + cpumask_to_string(_aff)};
		}
	}
	std::size_t element_size() const { return _element_size; }
	/* Run test in cpu_ against memory m_ */
	virtual std::tuple<cpu_time_t, std::size_t> run() const = 0;
};

class load_generator_8byte_read_random
	: private load_generator
{
	std::size_t _collection_size;
public:
	/* Construtor for any CPU-intensive setup prior to start of test
	 *   mem_: memory against which to run
	 *   cpu_ core to use
	 *   g_: geometry (to avoid running past end of memory when sz_ exceeds a cache line)
	 */
	explicit load_generator_8byte_read_random(std::shared_ptr<Pmem_access> mem_, int cpu_, const geometry_bounded &geo_)
		: load_generator{mem_, cpu_, geo_, sizeof(uint64_t)}
		, _collection_size{write_cycle(pm(), sizeof(uint64_t))}
	{
	}
	virtual ~load_generator_8byte_read_random() {}

	/* Run 8byte_read_random against memory m_ */
	std::tuple<cpu_time_t, std::size_t> run() 
	{
		set_affinity();
		return random_8byte_read_prepared(pm(), _collection_size);
	}
	static std::string type(std::size_t) { return "8" "byte_read_random"; }
};

class load_generator_8byte_write_random_persist
	: private load_generator
{
        std::vector<std::size_t> _positions;
public:
	/* Construtor for any CPU-intensive setup prior to start of test
	 *   mem_: memory against which to run
	 *   cpu_ core to use
	 *   g_: geometry (to avoid running past end of memory when sz_ exceeds a cache line)
	 */
	explicit load_generator_8byte_write_random_persist(std::shared_ptr<Pmem_access> mem_, int cpu_, const geometry_bounded &geo_)
		: load_generator{mem_, cpu_, geo_, sizeof(uint64_t)}
		, _positions{pattern_random{geo_}.generate(sizeof(uint64_t))}
	{
	}
	virtual ~load_generator_8byte_write_random_persist() {}

	/* Run 8byte_write_random against memory m_ */
	std::tuple<cpu_time_t, std::size_t> run() 
	{
		set_affinity();
		return std::tuple<cpu_time_t, std::size_t>{
			random_8byte_write_persist_prepared(pm(), _positions)
			, _positions.size()
		};
	}
	static std::string type(std::size_t) { return "8" "byte_write_random_persist"; }
};

class load_generator_memget
	: private load_generator
{
	std::shared_ptr<uint64_t> _dst;
	std::size_t _collection_size;
public:
	/* Construtor for any CPU-intensive setup prior to start of test
	 *   mem_: memory against which to run
	 *   cpu_ core to use
	 *   pat_: prototype for access pattern
	 *   sz_: number of bytes per memget
	 */
	explicit load_generator_memget(std::shared_ptr<Pmem_access> mem_, int cpu_, const geometry_bounded &geo_, const pattern &pat_, std::size_t sz_)
		: load_generator{mem_, cpu_, geo_, sz_}
		, _dst(new uint64_t[Common::div_round_up(sz_, sizeof(uint64_t))], std::default_delete<uint64_t[]>())
		, _collection_size{memcpy_init(pm(), pat_.generate(sz_), sz_)}
	{
	}
	virtual ~load_generator_memget() {}

	/* Run memset in cpu_ against memory m_, access pattern (vector of cache lines to write) */
	std::tuple<cpu_time_t, std::size_t> run() 
	{
		set_affinity();
		auto p{memget_run(_dst.get(), pm().data(), element_size(), _collection_size, sanity_check())};
		assert(std::get<1>(p) == _collection_size);
		return std::tuple<cpu_time_t, std::size_t>(std::get<0>(p), std::get<1>(p));
	}
	static std::string type(std::size_t sz_) { return "memget" + std::to_string(sz_); }
};

class load_generator_memset_persist
	: private load_generator
{
	std::vector<uint64_t> _positions;
	int _fillchar;
public:
	/* Construtor for any CPU-intensive setup prior to start of test
	 *   mem_: memory against which to run
	 *   cpu_ core to use
	 *   pat_: prototype for access pattern
	 *   sz_: number of bytes per memset
	 */
	explicit load_generator_memset_persist(std::shared_ptr<Pmem_access> mem_, int cpu_, const geometry_bounded &geo_, const pattern &pat_, std::size_t sz_, int fillchar_)
		: load_generator{mem_, cpu_, geo_, sz_}
		, _positions{pat_.generate(sz_)}
		, _fillchar{fillchar_}
	{
		if ( sanity_check() )
		{
			auto in_order = _positions;
			std::sort(in_order.begin(), in_order.end());
			assert(std::adjacent_find(in_order.begin(), in_order.end()) == in_order.end());
		}
	}
	virtual ~load_generator_memset_persist() {}

	/* Run memset in cpu_ against memory m_, access pattern (vector of cache lines to write) */
	std::tuple<cpu_time_t, std::size_t> run() 
	{
		set_affinity();
		return memset_run<persistence::PERSIST>(pm().data(), _positions, element_size(), _fillchar);
	}
	static std::string type(std::size_t sz_) { return "memset" + std::to_string(sz_) + "_persist"; }

};

#endif
