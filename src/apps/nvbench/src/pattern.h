#ifndef NVBENCH_PATTTERN_H
#define NVBENCH_PATTTERN_H

#include "geometry.h"

#include "round.h" /* div_round_up */
#include "wrapper_logging.h" /* PLOG */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

/*
 * A pattern is an access pattern. We use subclasses sequential, random
 */
class pattern
{
private:
	geometry _g;
protected:
	~pattern() {}
public:
	explicit pattern(const geometry &g_)
		: _g(g_)
	{}
	virtual const char *name() const = 0;
	/* elements in the generated vector are indices to an array of uint64_t */
	virtual std::vector<uint64_t> generate(std::size_t sz) const = 0;
	geometry geo() const { return _g; }
	bool sanity_check() const { return _g.sanity_check(); }
};

class pattern_sequential
	: public pattern
{
public:
	explicit pattern_sequential(const geometry &g_)
		: pattern{g_}
	{}
	virtual ~pattern_sequential() {}

	const char *name() const override { return "sequential"; }
	std::vector<uint64_t> generate(std::size_t sz_) const override
	{
		std::vector<uint64_t> positions;
		/* the number of "slots" (cache lines) spanned by an element of size sz_ */
		const auto slots_per_item = std::max(std::size_t(1), common::div_round_up(sz_, geometry::slot_size()));
		const auto uint64_per_item = slots_per_item * geometry::uint64_per_slot();
		const auto item_count = geo().uint64_count() / uint64_per_item;

		for ( std::size_t item = 0; item < item_count; ++item )
		{
			positions.push_back(item * uint64_per_item);
		}
		assert(geo().slot_count() == positions.size() * slots_per_item);

		if ( sanity_check() )
		{
			auto in_order = positions;
			std::sort(in_order.begin(), in_order.end());
			if ( std::adjacent_find(in_order.begin(), in_order.end()) != in_order.end() )
			{
				PLOG("duplicates in sequential size %lu uint64_per_item %zu from %lu to %lu count %lu", sz_, uint64_per_item, in_order.front(), in_order.back(), positions.size());
				assert(! "pattern_sequential has duplicates");
			}
		}

		return positions;
	}
};

class pattern_random
	: public pattern
{
public:
	explicit pattern_random(const geometry &g_)
		: pattern{g_}
	{}
	virtual ~pattern_random() {}
	const char *name() const override { return "random"; }
	std::vector<uint64_t> generate(std::size_t sz_) const override
	{
		/* the number of "slots" (cache lines) spanned by an element of size sz_ */
		const auto slots_per_item = std::max(std::size_t(1), common::div_round_up(sz_, geo().slot_size()));
		const auto uint64_per_item = slots_per_item * geometry::uint64_per_slot();
		const auto items_per_page = geo().slots_per_page()/slots_per_item;

		/* randomize within a page */
		std::vector<uint64_t> seq0;
		for ( size_t item_no = 0; item_no < items_per_page; ++item_no )
		{
			seq0.push_back(item_no * uint64_per_item);
		}

		std::random_shuffle(seq0.begin(), seq0.end());

		/* among pages */
		std::vector<uint64_t> seq1;

		/* ERROR: geo().slots_per_page() is the minimum stride. Will want more if sz_ is larger tham a page. */
		for ( size_t page_no = 0; page_no < geo().uint64_count()/geo().uint64_per_page(); ++page_no )
		{
			seq1.push_back(page_no*geo().uint64_per_page());
		}

		std::random_shuffle(seq1.begin(), seq1.end());

		std::vector<uint64_t> positions;

		/*
		* combine the two tiers. All pages are full pages; the last partial page, if any, is ignored.
		*/
		for ( const auto pg : seq1 )
		{
			for ( const auto off : seq0 )
			{
				const auto pos = pg + off;
				positions.push_back(pos);
			}
		}

		if ( sanity_check() )
		{
			auto in_order = positions;
			std::sort(in_order.begin(), in_order.end());
			if ( std::adjacent_find(in_order.begin(), in_order.end()) != in_order.end() )
			{
				PLOG("duplicates in pattern_random geo.slot_count %zu slots_per_item %zu slots_per_page %zu from %lu to %lu count %lu"
					,geo().slot_count(), slots_per_item, geo().slots_per_page(), in_order.front(), in_order.back(), positions.size());
			}
			assert(! "pattern_random has duplicates");
		}

		return positions;
	}
};

#endif
