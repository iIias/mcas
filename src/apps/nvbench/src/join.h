/*
 * (C) Copyright IBM Corporation 2018. All rights reserved.
 *
 * U.S. Government Users Restricted Rights - Use, duplication or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#ifndef _COMMON_JOIN_H_
#define _COMMON_JOIN_H_

#include <string>

namespace common
{
/* join a range of stringizable elements with a delimiter, after Perl/Python join */
	template <typename T>
		std::string join(std::string s, T first, const T last)
		{
			std::string r{};
			if ( first != last )
			{
				r += std::to_string(*first++);
			}
			while ( first != last )
			{
				r += s;
				r += std::to_string(*first++);
			}
			return r;
		}
}

#endif
