#ifndef NVBENCH_MEM_TEST_H
#define NVBENCH_MEM_TEST_H

#include "geometry.h"
#include "pmem_access.h"

#include <cstddef>
#include <memory>

template <typename T>
	void mem_no_init(T *, std::size_t)
	{
	}

/*
 * mem_test is a "memory under test", consisting of access to a memory
 * and a "geometry" specifying lower and upper "slots" within that memory.
 */
template <typename T>
	class mem_test
		: public Pmem_access
	{
		std::shared_ptr<Pmem_access> _a;
		geometry _g;
		mem_test(
			std::shared_ptr<Pmem_access> a
			, geometry g_
			, ptrdiff_t offset_
			, std::string id_
			, void (*init_)(T *, std::size_t)
		)
			: Pmem_access(
				static_cast<char *>(a->p_base()) + offset_
				, g_.slot_count() * g_.slot_size()
				, id_
			)
			, _a(a)
			, _g{g_}
		{
			init_(data(), size());
			if ( geo().sanity_check() )
			{
        //			PLOG("Mem test %s from %p for %zx", id_.c_str(), data(), size());
			}
			flush();
		}
	public:
		T *data() const { return static_cast<T *>(p_base()); }

		/* The constructor
		 * 1) make its own copy of access to the area
		 * 2) give the the memory elements a type (e.g. uint64_t)
		 * 3) flush the memory's cache (clflush_area, wmb).
		 */
		mem_test(
			std::shared_ptr<Pmem_access> a_
			, geometry_bounded g_
			, void (*init_)(T *, std::size_t) = mem_no_init<T>
		)
			: mem_test(
				a_
				, g_
				, g_.slot_min() * g_.slot_size()
				, a_->id() + ":" + std::to_string(g_.slot_min())
				, init_
			)
		{
		}
		mem_test(
			std::shared_ptr<Pmem_access> a_
			, geometry g_
			, void (*init_)(T *, std::size_t) = mem_no_init<T>
		)
			: mem_test(
				a_
				, g_
				, 0
				, a_->id() + ":0"
				, init_
			)
		{
		}
		void flush(const void *first, const void *last) const override { return _a->flush(first, last); }
		void flush() const { return _a->flush(p_base(), p_end()); }
		const char *type() const override { return _a->type(); }
		geometry geo() const { return _g; }
	};

#endif
