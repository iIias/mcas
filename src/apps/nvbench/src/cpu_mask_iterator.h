#ifndef NVBENCH_CPU_MASK_ITERATOR_H
#define NVBENCH_CPU_MASK_ITERATOR_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wnarrowing"
#pragma GCC diagnostic ignored "-Wconversion"

#include "wrapper_cpu.h"
#include "join.h"

#include <limits> /* numeric_limits */
#include <string>

class cpu_mask_iterator;

static bool operator==(const cpu_mask_iterator &a, const cpu_mask_iterator &b);

class cpu_mask_iterator
{
	cpu_mask_t *_m;
	int         _i;
public:
	void advance()
	{
		while ( _i != _m->size() * std::numeric_limits<unsigned char>::digits && ! _m->check_core(_i) )
		{
			++_i;
		}
	}
public:
	cpu_mask_iterator(cpu_mask_t &m_, int i_)
		: _m(&m_)
		, _i(i_)
	{
		advance();
	}
	int operator*() { return _i; }
	cpu_mask_iterator &operator++()
	{
		++_i;
		advance();
		return *this;
	}
	cpu_mask_iterator operator++(int)
	{
		const auto r = *this;
		++_i;
		advance();
		return r;
	}
	friend bool operator==(const cpu_mask_iterator &a, const cpu_mask_iterator &b);
};

static cpu_mask_iterator begin(cpu_mask_t &m)
{
	return cpu_mask_iterator{m, 0};
}

static cpu_mask_iterator end(cpu_mask_t &m)
{
	return cpu_mask_iterator{m, m.size() * std::numeric_limits<unsigned char>::digits};
}

static bool operator==(const cpu_mask_iterator &a, const cpu_mask_iterator &b)
{
	return a._i == b._i;
}

static bool operator!=(const cpu_mask_iterator &a, const cpu_mask_iterator &b) { return ! (a == b); }

static inline std::string cpumask_to_string(cpu_mask_t &m)
{
	return common::join(",", begin(m), end(m));
}

#pragma GCC diagnostic pop

#endif
