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

#include "heap_rc.h"

#include "clean_align.h"
#include "dax_manager.h"
#include "heap_rc_ephemeral.h"
#include "hstore_config.h"
#include "tracked_header.h"
#include "valgrind_memcheck.h"
#include <common/byte_span.h>
#include <common/string_view.h>
#include <common/utils.h>
#include <algorithm> /* max */
#include <cinttypes>
#include <memory> /* make_unique */
#include <stdexcept> /* range_error */
#include <string> /* to_string */

/* When used with ADO, this space apparently needs a 2MiB alignment.
 * 4 KiB alignment sometimes produces a disagreement between server and ADO mappings,
 * which manifests as incorrect key and data values as seen on the ADO side.
 */
heap_rc::heap_rc(
	unsigned debug_level_, byte_span pool0_full_, byte_span pool0_heap_, unsigned numa_node_, const string_view id_, const string_view backing_file_
)
	: heap(pool0_full_, pool0_heap_, numa_node_)
	, _tracked_anchor(debug_level_, &_tracked_anchor, &_tracked_anchor, sizeof(_tracked_anchor), sizeof(_tracked_anchor))
	, _eph(std::make_unique<heap_rc_ephemeral>(debug_level_, id_, backing_file_))
{
	void *last = ::end(pool0_heap_);
	if ( 0 < debug_level_ )
	{
		PLOG("%s: split %p .. %p) into segments", __func__, ::base(pool0_heap_), last);

		PLOG("%s: pool0 full %p: 0x%zx", __func__, ::base(_pool0_full), ::size(_pool0_full));
		PLOG("%s: pool0 heap %p: 0x%zx", __func__, ::base(_pool0_heap), ::size(_pool0_heap));
	}
	/* cursor now locates the best-aligned region */
	_eph->add_managed_region(_pool0_full, _pool0_heap, _numa_node);
	hop_hash_log<trace_heap_summary>::write(
		LOG_LOCATION
		, " pool ", ::base(_pool0_full), " .. ", ::end(_pool0_full)
		, " size ", ::size(_pool0_full)
		, " new"
	);
	VALGRIND_CREATE_MEMPOOL(::base(_pool0_heap), 0, false);
	persister_nupm::persist(this, sizeof(*this));
}

namespace
{
	using byte_span = common::byte_span;
	using string_view = common::string_view;
	auto open_region(const std::unique_ptr<nupm::dax_manager_abstract> &dax_manager_, string_view id_, unsigned numa_node_) -> byte_span
	{
		auto & iovs = dax_manager_->open_region(id_, numa_node_).address_map();
		if ( iovs.size() != 1 )
		{
			throw std::range_error("failed to re-open region " + std::string(id_));
		}
		return iovs.front();
	}
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-self"
#pragma GCC diagnostic ignored "-Wuninitialized"
heap_rc::heap_rc(
	unsigned debug_level_
	, const std::unique_ptr<nupm::dax_manager_abstract> &dax_manager_
	, const string_view id_
    , const string_view backing_file_
    , const std::uint64_t uuid_
	, const byte_span *iov_addl_first_
	, const byte_span *iov_addl_last_
)
	: heap(*this)
	, _tracked_anchor(this->_tracked_anchor)
	, _eph(std::make_unique<heap_rc_ephemeral>(debug_level_, id_, backing_file_))
{
	_eph->add_managed_region(_pool0_full, _pool0_heap, _numa_node);
	hop_hash_log<trace_heap_summary>::write(
		LOG_LOCATION
		, " pool ", ::base(_pool0_full), " .. ", ::end(_pool0_full)
		, " size ", ::size(_pool0_full)
		, " reconstituting"
	);

	VALGRIND_MAKE_MEM_DEFINED(::base(_pool0_heap), ::size(_pool0_heap));
	VALGRIND_CREATE_MEMPOOL(::base(_pool0_heap), 0, true);

	for ( auto r = iov_addl_first_; r != iov_addl_last_; ++r )
	{
		_eph->add_managed_region(*r, *r, _numa_node);
	}

	for ( std::size_t i = 0; i != _more_region_uuids_size; ++i )
	{
		auto r = open_region(dax_manager_, std::to_string(uuid_ + _more_region_uuids[i]), _numa_node);
		_eph->add_managed_region(r, r, _numa_node);
		VALGRIND_MAKE_MEM_DEFINED(::base(r), ::size(r));
		VALGRIND_CREATE_MEMPOOL(::base(r), 0, true);
	}
	_tracked_anchor.recover(debug_level_, _eph.get(), _numa_node);
}
#pragma GCC diagnostic pop

heap_rc::~heap_rc()
{
	quiesce();
}

auto heap_rc::regions() const -> nupm::region_descriptor
{
	return _eph->get_primary_region();
}

auto heap_rc::grow(
	const std::unique_ptr<nupm::dax_manager_abstract> & dax_manager_
	, std::uint64_t uuid_
	, std::size_t increment_
) -> std::size_t
{
	return heap::grow(_eph.get(), dax_manager_, uuid_, increment_);
}

void heap_rc::quiesce()
{
	hop_hash_log<trace_heap_summary>::write(LOG_LOCATION, " size ", ::size(_pool0_heap), " allocated ", _eph->allocated());
	_eph->write_hist<trace_heap_summary>(_pool0_heap);
	VALGRIND_DESTROY_MEMPOOL(::base(_pool0_heap));
	VALGRIND_MAKE_MEM_UNDEFINED(::base(_pool0_heap), ::size(_pool0_heap));
	_eph.reset(nullptr);
}

namespace
{
	/* Round up to (ceiling) power of 2, from Hacker's Delight 3-2 */
	std::size_t clp2(std::size_t sz_)
	{
		if ( sz_ != 0 )
		{
			--sz_;
			sz_ |= sz_ >> 1;
			sz_ |= sz_ >> 2;
			sz_ |= sz_ >> 4;
			sz_ |= sz_ >> 8;
			sz_ |= sz_ >> 16;
			sz_ |= sz_ >> 32;
		}
		return sz_ + 1;
	}
}

void *heap_rc::alloc(const std::size_t sz_, const std::size_t align_)
{
	auto align = clean_align(align_, sizeof(void *));

	auto sz = sz_;

	if ( sz < align )
	{
		/* round up only to a power of 2, so Rca_LB will find the element
		 * on free.
		 */
		sz = clp2(sz);
		assert( (sz & (sz - 1)) == 0 );
		/* Allocation must be a multiple of alignment. In the case,
		 * adjust alignment. */
		align = std::max(sizeof(void *), sz);
	}

	/* In any case, sz must be a multiple of alignment. */
	sz = (sz + align - 1U)/align * align;

	try {
		auto p = _eph->allocate(sz, _numa_node, align);
		/* Note: allocation exception from Rca_LB is General_exception, which does not derive
		 * from std::bad_alloc.
		 */

		VALGRIND_MEMPOOL_ALLOC(::base(_pool0_heap), p, sz);
		hop_hash_log<trace_heap>::write(LOG_LOCATION, "pool ", ::base(_pool0_full), " addr ", p, " align ", align_, " -> ", align, " size ", sz_, " -> ", sz);
		return p;
	}
	catch ( const std::bad_alloc & )
	{
		_eph->write_hist<true>(_pool0_heap);
		/* Sometimes lack of space will cause heap to throw a bad_alloc. */
		throw;
	}
	catch ( const General_exception &e )
	{
		_eph->write_hist<true>(_pool0_heap);
		/* Sometimes lack of space will cause heap to throw a General_exception with this explanation. */
		/* Convert to bad_alloc. */
		if ( e.cause() == string_view("region allocation out-of-space") )
		{
			throw std::bad_alloc();
		}
		throw;
	}
}

void *heap_rc::alloc_tracked(const std::size_t sz_, const std::size_t align_)
{
	/* alignment: enough for tracked_header prefix, and a power of 2 */
	auto align = clp2(std::max(clean_align(align_), sizeof(tracked_header)));

	/* size: a multiple of alignment */
	auto sz = round_up(sz_ + align, align);

	try {
		auto p = _eph->allocate(sz, _numa_node, align);
		/* Note: allocation exception from Rca_LB is General_exception, which does not derive
		 * from std::bad_alloc.
		 */

		VALGRIND_MEMPOOL_ALLOC(::base(_pool0_heap), p, sz);
		hop_hash_log<trace_heap>::write(LOG_LOCATION, "pool ", ::base(_pool0_full), " addr ", p, " align ", align_, " -> ", align, " size ", sz_, " -> ", sz);
		tracked_header *h = new (static_cast<char *>(p) + align - sizeof(tracked_header))
			tracked_header(_eph->debug_level(), &_tracked_anchor, _tracked_anchor._next, sz, align);
		persister_nupm::persist(h, sizeof *h);

		_tracked_anchor._next->_prev = h; /* _prev, need not flush */
		_tracked_anchor._next = h; /* _next, must flush */
		persister_nupm::persist(&_tracked_anchor._next, sizeof _tracked_anchor._next);

#if 0
		PLOG(
			"%s: TH %p prev %p next %p size %zu align %zu"
			, __func__
			, common::p_fmt(h)
			, common::p_fmt(h->_prev)
			, common::p_fmt(h->_next)
			, h->_size
			, h->_align
		);
#endif
		return h + 1;
	}
	catch ( const std::bad_alloc & )
	{
		_eph->write_hist<true>(_pool0_heap);
		/* Sometimes lack of space will cause heap to throw a bad_alloc. */
		throw;
	}
	catch ( const General_exception &e )
	{
		_eph->write_hist<true>(_pool0_heap);
		/* Sometimes lack of space will cause heap to throw a General_exception with this explanation. */
		/* Convert to bad_alloc. */
		if ( e.cause() == string_view("region allocation out-of-space") )
		{
			throw std::bad_alloc();
		}
		throw;
	}
}

void heap_rc::inject_allocation(const void * p, std::size_t sz_)
{
	auto alignment = sizeof(void *);
	sz_ = std::max(sz_, alignment);
	auto sz = (sz_ + alignment - 1U)/alignment * alignment;
	/* NOTE: inject_allocation should take a const void* */
	_eph->inject_allocation(const_cast<void *>(p), sz, _numa_node);
	VALGRIND_MEMPOOL_ALLOC(::base(_pool0_heap), p, sz);
	hop_hash_log<trace_heap>::write(LOG_LOCATION, "pool ", ::base(_pool0_heap), " addr ", p, " size ", sz);
}

void heap_rc::free(void *p_, std::size_t sz_, std::size_t alignment_)
{
	auto align = clean_align(alignment_, sizeof(void *));
	sz_ = std::max(sz_, align);
	auto sz = (sz_ + align - 1U)/align * align;
	VALGRIND_MEMPOOL_FREE(::base(_pool0_heap), p_);
	hop_hash_log<trace_heap>::write(LOG_LOCATION, "pool ", ::base(_pool0_heap), " addr ", p_, " size ", sz);
	return _eph->free(p_, sz, _numa_node);
}

void heap_rc::free_tracked(
	void *p_
	, std::size_t sz_
	, std::size_t // align_
)
{
	tracked_header *h = static_cast<tracked_header *>(p_)-1;
	auto align = h->_align;
	/* size: a multiple of alignment */
	auto sz = round_up(sz_ + align, align);
	if ( 3 < _eph->debug_level() )
	{
		PLOG(
			"%s: TH %p prev %p next %p size %zu align %zu"
			, __func__
			, common::p_fmt(h)
			, common::p_fmt(h->_prev)
			, common::p_fmt(h->_next)
			, h->_size
			, h->_align
		);
	}
	h->_next->_prev = h->_prev; /* _prev, need not flush */
	h->_prev->_next = h->_next; /* _next, must flush */
	persister_nupm::persist(&h->_prev->_next, sizeof h->_prev->_next);

	auto p = static_cast<char *>(p_) - h->_align;
	assert(sz == h->_size);
	VALGRIND_MEMPOOL_FREE(::base(_pool0_heap), p);
	hop_hash_log<trace_heap>::write(LOG_LOCATION, "pool ", ::base(_pool0_heap), " addr ", p, " size ", sz);
	return _eph->free(p, sz, _numa_node);
}

unsigned heap_rc::percent_used() const
{
    return _eph->capacity() == 0 ? 0xFFFFU : unsigned(_eph->allocated() * 100U / _eph->capacity());
}

bool heap_rc::is_reconstituted(const void * p_) const
{
	return _eph->is_reconstituted(p_);
}
