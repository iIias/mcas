#ifndef _NUMA_REMOTE_H_
#define _NUMA_REMOTE_H_

#include "pmem_access.h"

#include "wrapper_utils.h"
#include <numa.h>

#include <cerrno>
#include <cstddef>
#include <cstring> /* memset */
#include <string>
#include <system_error>

template <typename T>
	class numa_remote
		: public Pmem_access
	{
		std::string _type;
	public:
		T *data() const { return static_cast<T *>(p_base()); }
		numa_remote(std::size_t sz, std::string type_)
			: Pmem_access(sz)
			, _type(type_)
		{
			auto numa_all_local_cpus = numa_allocate_cpumask();
			if ( 0 != numa_sched_getaffinity(getpid(), numa_all_local_cpus) )
			{
				auto e = errno;
				throw std::system_error{std::error_code{e, std::system_category()}, std::string{"Failed numa_sched_getaffinity"}};

			}

			int local_cpu = 0; /* archetypical local CPU */
			for ( ; local_cpu != numa_num_configured_cpus() && ! numa_bitmask_isbitset(numa_all_local_cpus, local_cpu); ++local_cpu )
			{
			}
			numa_free_cpumask(numa_all_local_cpus); /* ERROR: leaks on throw */

			if ( local_cpu == numa_num_configured_cpus() )
			{
				throw std::range_error{"numa_remote: cannot identify a local cpu"};
			}

			auto node = 0;
			for ( ; node != numa_max_node() && node == numa_node_of_cpu(local_cpu); ++node )
			{
			}

			if ( node == numa_max_node() )
			{
				throw std::range_error{"numa_remote: cannot fine a remote numa memory"};
			}

			auto data = numa_alloc_onnode(sz, node);
			if ( ! data ) {
				auto e = errno;
				throw std::system_error{std::error_code{e, std::system_category()}, std::string{"Failed to numa_alloc_onnode "} + std::to_string(sz) + " bytes"};
			}
			set_p_base( data );
			std::memset(data, 0, sz); /* force pre-PF */
			clflush_area(data, sz);
			wmb();
		}
		~numa_remote() {
			numa_free(p_base(), size());
		}
		const char *type() const override { return _type.c_str(); }
	};

#endif
