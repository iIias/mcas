/*
 * (C) Copyright IBM Corporation 2017. All rights reserved.
 *
 * U.S. Government Users Restricted Rights - Use, duplication or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#ifndef COMMON_ROUND_H
#define COMMON_ROUND_H

#if defined __cplusplus

#include <cstddef>

namespace common
{
	constexpr auto div_round_down(std::size_t sz, std::size_t block) -> std::size_t
	{
		return sz/block;
	}

	constexpr auto div_round_up(std::size_t sz, std::size_t block) -> std::size_t
	{
		return static_cast<std::size_t>(sz + block - 1)/block;
	}

	constexpr auto round_down(std::size_t sz, std::size_t block) -> std::size_t
	{
		return div_round_down(sz, block) * block;
	}

	constexpr auto round_up(std::size_t sz, std::size_t block) -> std::size_t
	{
		return div_round_up(sz, block) * block;
	}
}

#endif

#endif
