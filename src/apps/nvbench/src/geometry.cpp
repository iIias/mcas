#include "geometry.h"

geometry_bounded geometry::bound(std::size_t lo, std::size_t hi) const
{
	return geometry_bounded{lo, hi, slots_per_page(), sanity_check()};
}
