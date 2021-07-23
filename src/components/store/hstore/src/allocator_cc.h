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


#ifndef MCAS_HSTORE_ALLOCATOR_CC_H
#define MCAS_HSTORE_ALLOCATOR_CC_H

#include "deallocator_cc.h"

#include "alloc_key.h" /* AK_ACTUAL */
#include "bad_alloc_cc.h"
#include "heap_access.h"
#include "persister_cc.h"
#include "persistent.h"

#include <cstddef> /* size_t, ptrdiff_t */

template <typename T, typename Heap, typename Persister>
	struct allocator_cc;

template <typename Heap, typename Persister>
	struct allocator_cc<void, Heap, Persister>
		: public deallocator_cc<void, Heap, Persister>
	{
		using deallocator_type = deallocator_cc<void, Heap, Persister>;
		using typename deallocator_type::value_type;
	};

template <typename T, typename Heap, typename Persister = persister>
	struct allocator_cc
		: public deallocator_cc<T, Heap, Persister>
	{
		using deallocator_type = deallocator_cc<T, Heap, Persister>;
		using typename deallocator_type::heap_type;
		using typename deallocator_type::size_type;
		using typename deallocator_type::value_type;
		using typename deallocator_type::pointer_type;

		allocator_cc(const heap_access<heap_type> &pool_, Persister p_ = Persister()) noexcept
			: deallocator_type(pool_, (p_))
		{}

		allocator_cc(const allocator_cc &a_) noexcept = default;

		template <typename U, typename P>
			allocator_cc(const allocator_cc<U, Heap, P> &a_) noexcept
				: allocator_cc(a_.pool())
			{}

		allocator_cc &operator=(const allocator_cc &a_) = delete;

		void arm_extend()
		{
			this->pool()->extend_arm();
		}
		void disarm_extend()
		{
			this->pool()->extend_disarm();
		}

		void allocate(
			AK_ACTUAL
			pointer_type & p_
			, size_type s_
			, size_type alignment_
		)
		{
			this->pool()->alloc(reinterpret_cast<persistent_t<void *> *>(&p_), s_ * sizeof(T), alignment_);
			/* Error: this check is too late;
			 * most of the intersting information is gone.
			 */
			if ( p_ == 0 )
			{
				throw bad_alloc_cc(AK_REF alignment_, s_, sizeof(T));
			}
		}

		/* allocate and remember the allocation. heap_access<heap_type> does this for every allocation */
		void allocate_tracked(
			AK_ACTUAL
			pointer_type & p_
			, size_type s_
			, size_type alignment_
		)
		{
			allocate(AK_REF p_, s_, alignment_);
		}
	};

#endif
