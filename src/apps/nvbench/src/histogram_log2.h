/*
 * (C) Copyright IBM Corporation 2018. All rights reserved.
 *
 * U.S. Government Users Restricted Rights - Use, duplication or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#ifndef _COMMON_HISTOGRAM_LOG2_H_
#define _COMMON_HISTOGRAM_LOG2_H_

#include <array>
#include <limits>

namespace common
{
	template <typename clz_arg_t>
		int clz(clz_arg_t);

	template <>
		int clz(unsigned v)
		{
			/* Note: If no builtin clz, see deBruijn sequence method, or Hacker's Delight section 5-3 */
			return __builtin_clz(v);
		}

	template <>
		int clz(unsigned long v)
		{
			/* Note: If no builtin clzl, see deBruijn sequence method, or Hacker's Delight section 5-3 */
			return __builtin_clzl(v);
		}

template <typename clz_arg_t>
	class histogram_log2
	{
	public:
		static auto constexpr array_size = std::numeric_limits<clz_arg_t>::digits;
		using array_t = std::array<unsigned, array_size>;
	private:
		array_t _hist;
	public:
		histogram_log2()
			: _hist{}
		{}

		void enter(clz_arg_t v) {
			++_hist[ v ? array_size - clz(v) : 0];
		}

		const array_t &hist() const { return _hist; }
	};
}

#endif
