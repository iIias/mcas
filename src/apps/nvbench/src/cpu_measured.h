#ifndef NVBENCH_CPU_MEASURED_H
#define NVBENCH_CPU_MEASURED_H

#include "wrapper_cpu.h"

class cpu_measured
{
	double _cycles_per_nsec;
public:
	explicit cpu_measured(double cycles_per_nsec_)
		: _cycles_per_nsec{cycles_per_nsec_}
	{}

	double tsc_to_nsec(cpu_time_t duration) const
	{
		return double(duration)/_cycles_per_nsec;
	}

	double ghz() const
	{
		return 1.0/_cycles_per_nsec;
	}
};

#endif
