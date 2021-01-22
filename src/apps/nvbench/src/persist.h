#ifndef NVBENCH_PERSIST_H
#define NVBENCH_PERSIST_H

#if __has_include(<libpmem.h>)
#include "pmem_helper.h"
#include "pmem_xms_helper.h"
#define PERSIST_POSSIBLE true
#else
#define PERSIST_POSSIBLE false
#endif

#if ! PERSIST_POSSIBLE
#include <cstring>
#include <cstddef>
void pmem_persist(const void *, std::size_t) {}
void pmem_memcpy_persist(void *d, const void *s, std::size_t n) { std::memcpy(d,s,n); }
void pmem_memset_persist(void *d, int c, std::size_t n) { std::memset(d,c,n); }
void pmem_memcpy_nodrain(void *d, const void *s, std::size_t n) { std::memcpy(d,s,n); }
void pmem_memset_nodrain(void *d, int c, std::size_t n) { std::memset(d,c,n); }
#endif

template <typename T>
	void persist(const T &t) {
		return pmem_persist(&t, sizeof t);
	}

#endif
