#ifndef _NUMA_LOCAL_H_
#define _NUMA_LOCAL_H_

#include "numa_dram.h"

#include <numa.h> /* numa_alloc_local */

#include <cerrno>
#include <cstddef>
#include <string>
#include <system_error>

template <typename T>
	class numa_local
		: public numa_dram<T>
	{
		static void *alloc(std::size_t sz_)
		{
			auto data = numa_alloc_local(sz_);
			if ( ! data ) {
				auto e = errno;
				throw std::system_error{
					std::error_code{e, std::system_category()}
					, std::string{"Failed to numa_alloc_local "} + std::to_string(sz_) + " bytes"
				};
			}
			return data;
		}
	public:
		numa_local(std::size_t sz_, std::string type_)
			: numa_dram<T>(alloc(sz_), sz_, type_)
		{
		}
	};

#endif
