#ifndef _NUMA_ON_NODE_H_
#define _NUMA_ON_NODE_H_

#include "numa_dram.h"

#include "wrapper_utils.h"
#include <numa.h>

#include <cerrno>
#include <cstddef>
#include <cstring> /* memset */
#include <string>
#include <system_error>

template <typename T>
	class numa_on_node
		: public numa_dram<T>
	{
#if 0
		std::string _type;
#endif
		static void *alloc(std::size_t sz_, int node_)
		{
			auto data = numa_alloc_onnode(sz_, node_);
			if ( ! data )
			{
				auto e = errno;
				throw std::system_error{
					std::error_code{e, std::system_category()}
					, std::string{"Failed to numa_alloc_on_node "} + std::to_string(sz_) + " bytes on " + std::to_string(node_)
				};
			}
			return data;
		}
	public:
		numa_on_node(std::size_t sz_, int node_, std::string type_)
			: numa_dram<T>(alloc(sz_, node_), sz_, type_, node_)
		{
		}
	};

#endif
