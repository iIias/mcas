#ifndef REPORTER_RUSAGE_H
#define REPORTER_RUSAGE_H

#include "wrapper_logging.h" /* PLOG */

#include <sys/time.h> /* rusage */
#include <sys/resource.h> /* rusage */

#include <cstring>
#include <ostream>
#include <string>

/* reporter_rusage reports rusage statistics for a section of code,
 * in an attempt to determine whether context switches or page faults
 * affect the benchmarks. Stats:
 *  - minor page faults
 *  - major page faults
 *  - voluntary context switches
 *  - involuntary context switches
 */
class reporter_rusage
{
	std::string _tag;
	rusage _r;
	bool _good;
	std::ostream *_out;
	void error_check(int e) {
		if ( ! _good ) {
			PLOG("rusage failure: %s", strerror(e));
		}
	}
public:
	reporter_rusage(const char *tag_, std::ostream *o_)
		: _tag(tag_)
		, _r{}
		, _good{0 == getrusage(RUSAGE_SELF, &_r)}
		, _out(o_)
	{
		error_check(errno);
	}
	reporter_rusage(const char *tag_)
		: reporter_rusage(tag_, nullptr)
	{
	}
	reporter_rusage(const reporter_rusage &) = delete;
	reporter_rusage &operator=(const reporter_rusage &) = delete;
	~reporter_rusage() {
		rusage r1;
		if ( _good && 0 == getrusage(RUSAGE_SELF, &r1) ) {
			if ( _out )
			{
				*_out << "tag" << _tag << " minflt/majflt/nvcsw/nivcsw "
					<< r1.ru_minflt-_r.ru_minflt << "/" << r1.ru_majflt-_r.ru_majflt << "/" << r1.ru_nvcsw-_r.ru_nvcsw << "/" << r1.ru_nivcsw-_r.ru_nivcsw << " "
					;
			}
			else
			{
				PLOG("tag %s minflt/majflt/nvcsw/nivcsw %ld/%ld/%ld/%ld"
					, _tag.c_str()
					, r1.ru_minflt-_r.ru_minflt
					, r1.ru_majflt-_r.ru_majflt
					, r1.ru_nvcsw-_r.ru_nvcsw
					, r1.ru_nivcsw-_r.ru_nivcsw
				);
			}
		}
	}
	bool good() const { return _good; }
	void reset() {
		_good = 0 == getrusage(RUSAGE_SELF, &_r);
		error_check(errno);
	}
};

#endif
