#ifndef NVBENCH_GEOMETRY_H
#define NVBENCH_GEOMETRY_H

#include <cstddef> /* size_t */
#include <cstdint> /* uint64_t */

class geometry_bounded;

class geometry
{
	std::size_t _slot_count;
	std::size_t _slots_per_page;
	bool _sanity_check;
	static constexpr size_t SLOT_SIZE = 64;
public:
	explicit geometry(
		std::size_t slot_count_
		, std::size_t slots_per_page_
		, bool sanity_check_
	)
		: _slot_count{slot_count_}
		, _slots_per_page{slots_per_page_}
		, _sanity_check{sanity_check_}
	{}
	std::size_t slots_per_page() const { return _slots_per_page; }
	std::size_t uint64_per_page() const { return slots_per_page()*uint64_per_slot(); }
	bool sanity_check() const { return _sanity_check; }
	std::size_t slot_count() const { return _slot_count; }
	std::size_t uint64_count() const { return _slot_count*uint64_per_slot(); }
	geometry_bounded bound(std::size_t lo, std::size_t hi) const;
	static std::size_t constexpr slot_size() { return SLOT_SIZE; }
	static std::size_t constexpr min_io_size() { return SLOT_SIZE; }
	static std::size_t constexpr uint64_per_slot() { return min_io_size()/sizeof(uint64_t); }
};

class geometry_bounded
	: public geometry
{
	std::size_t _slot_min;
	std::size_t _slot_mac;
public:
	explicit geometry_bounded(
		std::size_t slot_min_
		, std::size_t slot_mac_
		, std::size_t slots_per_page_
		, bool sanity_check_
	)
		: geometry{slot_mac_ - slot_min_, slots_per_page_, sanity_check_}
		, _slot_min{slot_min_}
		, _slot_mac{slot_mac_}
	{}
	std::size_t slot_min() const { return _slot_min; }
	std::size_t slot_mac() const { return _slot_mac; }
};

#endif
