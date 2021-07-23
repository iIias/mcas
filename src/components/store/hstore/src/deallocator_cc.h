/*
   Copyright [2017-2021] [IBM Corporation]
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/


#ifndef MCAS_HSTORE_DEALLOCATOR_CC_H
#define MCAS_HSTORE_DEALLOCATOR_CC_H

#include "hstore_config.h"
#include "heap_access.h"
#include "persistent.h"
#include "persister_cc.h"

#include <cstddef> /* size_t, ptrdiff_t */
#include <type_traits> /* true_type, false_type */

template <typename T, typename Heap, typename Persister>
	struct deallocator_cc;

template <typename Heap, typename Persister>
	struct deallocator_cc<void, Heap, Persister>
	{
		using value_type = void;
	};

template <typename T, typename Heap, typename Persister = persister>
	struct deallocator_cc
		: public Persister
	{
		using heap_type = Heap;
	private:
		heap_access<heap_type> _pool;
	public:
		using value_type = T;
		using persister_type = Persister;
		using size_type = std::size_t;
		using pointer_type = persistent_t<value_type *>;
		using propagate_on_container_move_assignment = std::true_type;
		using is_always_equal = std::false_type;

		explicit deallocator_cc(const heap_access<heap_type> &pool_, Persister p_ = Persister()) noexcept
			: Persister(p_)
			, _pool(pool_)
		{}

		explicit deallocator_cc(const deallocator_cc &) noexcept = default;

		template <typename U, typename P>
			explicit deallocator_cc(const deallocator_cc<U, Heap, P> &d_) noexcept
				: deallocator_cc(d_.pool())
			{}

		deallocator_cc &operator=(const deallocator_cc &e_) = delete;

		void emplace_arm()
		{
			_pool->emplace_arm();
		}

		void emplace_disarm()
		{
			_pool->emplace_disarm();
		}

		/* For crash consistency testing only.
		 * Some deallocations are for pointers which are themselves
		 * marked persistent, and which are passed as such to the
		 * deallocator. */
		/* ERROR: ensure that all calls from the kvstore come from an
		 * allocator with crash-consistent logic
		 */
		void deallocate(
			pointer_type & p_
			, size_type sz_
		)
		{
			/* What we might like to say, if persistent_t had the intelligence:
			 * _pool->free(static_pointer_cast<void *>(&p), sizeof(T) * sz_);
			 */
			_pool->free(reinterpret_cast<persistent_t<void *> *>(&p_), sizeof(T) * sz_);
		}

		/* Deallocate a "tracked" allocation */
		void deallocate_tracked(
			pointer_type & p_
			, size_type sz_
		)
		{
			deallocate(p_, sz_);
		}

		void persist(const void *ptr, size_type len, const char * = nullptr) const
		{
			persister_type::persist(ptr, len);
		}

		auto pool() const
		{
			return _pool;
		}
	};

#endif
