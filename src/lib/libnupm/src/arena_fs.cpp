/*
   Copyright [2017-2020] [IBM Corporation]
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

#include "arena_fs.h"

#include "filesystem.h"

#include <nupm/dax_manager.h>
#include <common/fd_open.h>
#include <common/memory_mapped.h>
#include <common/utils.h>

#include <fcntl.h> /* ::open, ::posix_fallocate */
#include <boost/scope_exit.hpp>
#include <sys/mman.h> /* ::mmap */
#include <sys/stat.h> /* ::open */
#include <cinttypes>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

static constexpr unsigned MAP_LOG_GRAIN = 21U;
static constexpr int MAP_HUGE = MAP_LOG_GRAIN << MAP_HUGE_SHIFT;

#ifndef MAP_SYNC
#define MAP_SYNC 0x80000
#endif

#if _NUPM_FILESYSTEM_STD_
namespace fs = std::filesystem;
#else
namespace fs = std::experimental::filesystem;
#endif

std::vector<common::memory_mapped> arena_fs::fd_mmap(int fd, const std::vector<byte_span> &map, int flags, ::off_t offset)
{
	std::vector<common::memory_mapped> mapped_elements;
	for ( const auto &e : map )
	{
		using namespace nupm;
		mapped_elements.emplace_back(
			e
			, PROT_READ | PROT_WRITE
			, flags
			, fd
			, offset
		);

		if ( ! mapped_elements.back() )
		{
			mapped_elements.pop_back();
			flags &= ~MAP_SYNC;
			mapped_elements.emplace_back(
				e
				, PROT_READ | PROT_WRITE
				, flags
				, fd
				, offset
			);
		}

		if ( ! ::data(mapped_elements.back()) )
		{
			auto er = int(::size(mapped_elements.back()));
			throw General_exception("%s: mmap failed for fsdax (request %p:0x%zu): %s", __func__, ::base(e), ::size(e), ::strerror(er));
		}

		if ( ::madvise(::data(e), ::size(e), MADV_DONTFORK) != 0 )
		{
			auto er = errno;
			throw General_exception("%s: madvise 'don't fork' failed for fsdax (%p %lu): %s", __func__, ::base(e), ::size(e), ::strerror(er));
		}

		offset += ::size(e);
	}

	return mapped_elements;
}

auto arena_fs::get_mapping(const path &path_map) -> std::pair<std::vector<byte_span>, std::size_t>
{
	/* A region must always be mapped to the same address, as MCAS
	 * MCAS software uses absolute addresses. Current design is to
	 * save this in a file extended attribute, ahtough it could be
	 * saved in a specially-named file.
	 */
	std::vector<byte_span> m;
	std::ifstream f(path_map.c_str());
	std::size_t covered = 0;
	std::uint64_t addr;
	std::size_t size;
	f.unsetf(std::ios::dec|std::ios::hex|std::ios::oct);
	f >> addr >> size;
	while ( f.good() )
	{
		m.push_back(common::make_byte_span(reinterpret_cast<void *>(addr), size));
		covered += size;
#if 0
		PLOG("%s %s: %p, 0x%zx", __func__, path_map.c_str(), m.back().data(), m.back().size());
#endif
		f >> addr >> size;
	}
	return { m, covered };
}

auto arena_fs::get_mapping(const path &path_map, const std::size_t expected_size) -> std::vector<byte_span>
{
	auto r = get_mapping(path_map);
	if ( r.second != expected_size )
	{
		std::ostringstream o;
		o << __func__ << ": map file " << path_map << std::hex << std::showbase << " expected to cover " << expected_size << " bytes, but covers " << r.second << " bytes";
		throw std::runtime_error(o.str());
	}
	return r.first;
}

arena_fs::arena_fs(const common::log_source &ls_, path dir_)
  : arena(ls_)
  , _dir(dir_)
{
  CPLOG(2, "%s debug level %u", __func__, debug_level());
}

void arena_fs::debug_dump() const
{
  PLOG("%s::%s: fsdax directory %s", _cname, __func__, _dir.c_str());
}

/* used only inside region_create */
void *arena_fs::region_create_inner(
	common::fd_locked &&fd
	, const string_view id_
	, gsl::not_null<registry_memory_mapped *> const mh_
	, const std::vector<byte_span> &mapping_
)
try {
	auto entered = mh_->enter(std::move(fd), id_, mapping_);
	/* return the map key, or nullptr if already mapped */
	return entered ? ::base(mapping_.front()) : nullptr;
}
catch ( const std::runtime_error &e )
{
	PLOG("%s: %.*s failed %s", __func__, int(id_.size()), id_.begin(), e.what());
	return nullptr;
}

auto arena_fs::region_get(const string_view id_) -> region_descriptor
{
  return region_descriptor(id_, path_data(id_).string(), get_mapping(path_map(id_)).first);
}

auto arena_fs::region_create(
	const string_view id_
	, gsl::not_null<registry_memory_mapped *> const mh_
	, std::size_t size
) -> region_descriptor
{
	/* A region is a file the the region_path directory.
	 * The file name is the id_.
	 */

	fs::create_directories(path_data(id_).remove_filename());

	common::fd_locked fd(::open(path_data(id_).c_str(), O_CREAT|O_EXCL|O_RDWR, 0666));

	if ( fd < 0 )
	{
		auto e = errno;
		PLOG("%s %i = open %s failed: %s", __func__, fd.fd(), path_data(id_).c_str(), ::strerror(e));
		return region_descriptor();
	}
	CPLOG(1, "%s %i = open %s", __func__, fd.fd(), path_data(id_).c_str());

	auto path_data_local = path_data(id_);
	auto path_map_local = path_map(id_);

	/* file is created and opened */
	bool commit = false;

	BOOST_SCOPE_EXIT(&commit, &path_data_local) {
		if ( ! commit ) { ::unlink(path_data_local.c_str()); }
	} BOOST_SCOPE_EXIT_END

	/* Every region segment needs a unique address range. locate_free_address_range provides one.
	 */

	size = round_up_t(size, 1U<<21U);
	auto base_addr = mh_->locate_free_address_range(size);

	/* Extend the file to the specified size */
	auto e = ::posix_fallocate(fd.fd(), 0, ::off_t(size));
	if ( e != 0 )
	{
		PLOG("%s::%s posix_fallocate: %zu: %s", _cname, __func__, size, strerror(e));
		return region_descriptor();
	}
	CPLOG(1, "%s posix_fallocate %i to %zu", __func__, fd.fd(), size);

	{
		std::ofstream f(path_map_local.c_str(), std::ofstream::trunc);
		CPLOG(1, "%s: write %s", __func__, path_map_local.c_str());
		f << std::showbase << std::hex << base_addr << " " << size << std::endl;
	}

	path map_path_local = path_map(id_);

	BOOST_SCOPE_EXIT(&commit, &map_path_local) {
		if ( ! commit )
		{
			std::error_code ec;
			fs::remove(map_path_local, ec);
		}
	} BOOST_SCOPE_EXIT_END;

	using namespace nupm;

	auto v = region_create_inner(std::move(fd), id_, mh_, std::vector<byte_span>({common::make_byte_span(base_addr, size)}));
	if ( v )
	{
		commit = true;
	}
	return
		region_descriptor(
			id_
			, path_data_local.string()
			, region_descriptor::address_map_t(1, common::make_byte_span(v, size))
		);
}

void arena_fs::region_resize(
	gsl::not_null<space_registered *> const sr_
	, std::size_t size_
)
{
	auto path_data_local = path_data(sr_->path_name());
	auto path_map_local = path_map(sr_->path_name());
	size_ = round_up_t(size_, 1U<<21U);
	auto r = get_mapping(path_map_local);
	CPLOG(2, "%s: %s current size %zu, requested size %zu", __func__, sr_->path_name().c_str(), r.second, size_)
	if ( r.second < size_ )
	{
		/* grow: truncate, then add to mapping file, add to in-memory mmap list */

		/* Every region segment needs a unique address range. locate_free_address_range provides one.
		 */

/* Section which could be moved inside space_registered::grow, if space_registered were specialized for fsdax */
		std::size_t added_size = size_ - r.second;
		auto added_base_addr = sr_->_or.range().dm()->locate_free_address_range(added_size);

		{
			/* Extend the file to the specified size */
			auto e = ::posix_fallocate(sr_->_or.fd(), 0, ::off_t(size_));
			if ( e != 0 )
			{
				PLOG("%s::%s posix_fallocate: failed %zu: %s", _cname, __func__, size_, strerror(e));
				return;
			}
			CPLOG(1, "%s posix_fallocate %i to %zu", __func__, sr_->_or.fd(), size_);
		}

		{
			std::ofstream f(path_map_local.c_str(), std::ofstream::app);
			CPLOG(1, "%s: write %s", __func__, path_map_local.c_str());
			f << std::showbase << std::hex << added_base_addr << " " << added_size << std::endl;
		}
/* End of section which could be moved inside space_registered::grow, if space_registered were specialized for fsdax */
		sr_->_or.grow(std::vector<byte_span>(1, common::make_byte_span(added_base_addr, added_size)));
	}
	else if ( size_ < r.second )
	{
		/* shrink: shrink from in-memory mmap list. subtract from mapping, then truncate.
		 * Or do nothing, as the resize is advisory.
         */
		std::size_t removed_size = r.second - size_;

		sr_->_or.shrink(removed_size);
/* Section which could be moved inside space_registered::shrink, if space_registered were specialized for fsdax */
		auto map_size_to_remove = removed_size;
		while ( 0 != map_size_to_remove )
		{
			if ( map_size_to_remove < ::size(r.first.back()) )
			{
				r.first.back() = common::make_byte_span(::base(r.first.back()), ::size(r.first.back()) - map_size_to_remove);
				map_size_to_remove = 0;
			}
			else
			{
				map_size_to_remove -= ::size(r.first.back());
				r.first.pop_back();
			}
		}
		{
			std::ofstream f(path_map_local.c_str(), std::ofstream::trunc);
			CPLOG(1, "%s: write %s", __func__, path_map_local.c_str());
			for ( const auto &iov : r.first )
			{
				f << std::showbase << std::hex << ::base(iov) << " " << ::size(iov) << std::endl;
			}
		}

		{
			/* Shrink the file to the specified size */
			auto e = ::posix_fallocate(sr_->_or.fd(), 0, ::off_t(size_));
			if ( e != 0 )
			{
				PLOG("%s::%s posix_fallocate: failed %zu: %s", _cname, __func__, size_, strerror(e));
			}
			CPLOG(1, "%s posix_fallocate %i to %zu", __func__, sr_->_or.fd(), size_);
		}
/* End of section which could be moved inside space_registered::grow, if space_registered were specialized for fsdax */
	}
}

void arena_fs::region_erase(const string_view id_, gsl::not_null<registry_memory_mapped *> mh_)
{
	auto path_data_local = path_data(id_);
	CPLOG(1, "%s remove %s", __func__, path_data_local.c_str());
	fs::remove(path_data(id_));
	CPLOG(1, "%s remove %s", __func__, path_map(id_).c_str());
	fs::remove(path_map(id_));
	mh_->remove(id_);
}

std::size_t arena_fs::get_max_available()
{
  return 0; /* .. until someone needs an actual value */
}
