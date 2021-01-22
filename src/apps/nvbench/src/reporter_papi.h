#ifndef REPORTER_PAPI_H
#define REPORTER_PAPI_H

#include "wrapper_logging.h" /* PLOG */

#if __has_include(<papi.h>)
#include <papi.h>
#define HAS_PAPI true
#else
#define HAS_PAPI false
#define PAPI_OK 0
namespace
{
  int PAPI_start_counters(
    int * // events
    , int  // array_leni
  ) { return -1; }
  int PAPI_stop_counters(
    long long * // values
    , int  // array_len
  ) { return -1; }
  int PAPI_num_counters() { return 0; }
  const char *PAPI_strerror(int) { return "No PAPI"; }
}
#define PAPI_L1_LDM 0
#define PAPI_L2_LDM 0
#define PAPI_L3_LDM 0
#define PAPI_L1_DCH 0
#define PAPI_L2_DCH 0
#define PAPI_L3_DCH 0
#define PAPI_L3_DCR 0
#define PAPI_TLB_DM 0
#endif

#include <algorithm>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#if HAS_PAPI
#define E(X) {#X, X},
static const std::map<std::string, int> f_map {
#include "papi_list.h"
};

#undef E

#define E(X) {X, #X},
static const std::map<int, std::string> r_map {
#include "papi_list.h"
};
#endif

/* reporter_papi reports performance counter statistics for a section of code,
 * in an attempt to determine cache usage
 * Stats:
 *  - PAPI_L1_LDM L1 cache load misses
 *  - PAPI_L2_LDM L2 cache load misses
 *  - PAPI_L3_LDM L3 cache load misses
 *  - PAPI_L1_DCH L1 data cache hits
 *  - PAPI_L2_DCH L2 data cache hits
 *  - PAPI_L3_DCH L3 data cache hits
 */
class reporter_papi
{
	using stat_t = std::vector<long long>;

	std::string _tag;
	std::vector<int> _list;
	stat_t _stats;
	int _num_used_ctrs;
	int _papi_rc;
	std::ostream *_out;
	void error_check(const char *who)
	{
		if ( _papi_rc != PAPI_OK )
		{
			PLOG("papi failure (from %s): %s", who, PAPI_strerror(_papi_rc));
		}
	}
	int start_counters(const int *first_, const int *last_)
	{
		std::vector<int> list(first_, last_);
		PLOG("papi start %d counters", _num_used_ctrs);
		return PAPI_start_counters(&*list.begin(), _num_used_ctrs);
	}
	int start_counters()
	{
		return start_counters(&*std::begin(_list), &*std::end(_list));
	}
public:
	reporter_papi(const char *tag, std::ostream *o_)
		: _tag(tag)
		, _list{
			PAPI_L3_LDM
			, PAPI_L3_DCR
			, PAPI_TLB_DM
		}
		, _stats(_list.size(), 0)
		, _num_used_ctrs{std::min(PAPI_num_counters(),int(_stats.size()))}
		, _papi_rc{start_counters()}
		, _out(o_)
	{
		error_check("constructor");
	}
	reporter_papi(const reporter_papi &) = delete;
	reporter_papi &operator=(const reporter_papi &) = delete;

	reporter_papi(const char *tag_)
		: reporter_papi(tag_, nullptr)
	{}

	~reporter_papi()
	{
		if ( PAPI_OK == PAPI_stop_counters(&*_stats.begin(), int(_stats.size())) )
		{
#if HAS_PAPI
			std::string ps { _tag };
			for ( auto i = 0U; i != _list.size(); ++i )
			{
				auto desc = r_map.find(_list[i]);
				ps += " " + desc->second + ":" + std::to_string(_stats[i]);
			}

			if ( _out )
			{
				*_out << "papi " << ps << "\n";
			}
			else
			{
				PLOG("papi %s", ps.c_str());
			}
#endif
		}
	}
	bool good() const { return _papi_rc == PAPI_OK; }
	void reset()
	{
		std::fill(_stats.begin(), _stats.end(), 0LL);
		_papi_rc = start_counters();
		error_check("reset");
	}
};

#endif
