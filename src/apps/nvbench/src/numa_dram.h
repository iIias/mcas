#ifndef _NUMA_DRAM_H_
#define _NUMA_DRAM_H_

#include "pmem_access.h"

#include "wrapper_logging.h"
#include "wrapper_utils.h"
#include <numa.h> /* numa_free */

#include <cstddef>
#include <cstring> /* memset */
#include <string>

template <typename T>
	class numa_dram
		: public Pmem_access
	{
		std::string _type;
	public:
		T *data() const { return static_cast<T *>(p_base()); }

		numa_dram(void *data_, std::size_t sz_, std::string type_, int node_)
			: Pmem_access(data_, sz_, node_)
			, _type(type_)
		{
			std::memset(data_, 0, sz_); /* force pre-PF */
			Pmem_access::flush();
		}

		numa_dram(void *data_, std::size_t sz_, std::string type_)
			: numa_dram<T>(data_, sz_, type_, -1)
		{
		}

		void flush(const void *first, const void *last) const override
		{
			auto size = ptrdiff_t(static_cast<const char *>(last) - static_cast<const char *>(first));
			if ( 0 < size )
			{
				clflush_area(const_cast<void *>(first), size);
#if 0
				PLOG("clflush_area %p %p", first, last);
#endif
				wmb();
			}
		}

		~numa_dram() {
			numa_free(p_base(), size());
		}

		const char *type() const override { return _type.c_str(); }
	};

#endif
