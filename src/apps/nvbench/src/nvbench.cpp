#include "numa_local.h"
#include "numa_on_node.h"

#include "cpu_measured.h"
#include "histogram_linear.h" /* histogram */
#include "load_generator.h"
#include "external_count.h"
#include "memcpy_init.h"
#include "reporter_rusage.h"
#include "reporter_papi.h"
#include "rw_prepared.h"
#include "geometry.h"
#include "pattern.h"
#include "persist.h"

#include <boost/program_options.hpp>

#include <common/exceptions.h> /* Exception */
#include "wrapper_logging.h" /* PLOG */
#include "round.h"
#include "histogram_log2.h"
#include "join.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring> /* memcpy */
#include <future>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

void flush(void *dst, std::size_t size)
{
  if ( 0 < size )
  {
    clflush_area(dst, size);
    wmb();
  }
}

void *memcpy_and_flush(void *dst, const void *src, std::size_t size)
{
  auto r = std::memcpy(dst, src, size);
  flush(dst, size);
  return r;
}

void *memset_and_flush(void *dst, int src, std::size_t size)
{
  auto r = std::memset(dst, src, size);
  flush(dst, size);
  return r;
}

/* Remove // comment to ensable rusage stats or papi stats or both */
#define GATE_RUSAGE(x) // ,x
#define GATE_PAPI(x) // ,x

const std::string pmem_file_0 = "nvbench.dat";
const std::string pmem_file_1 = "nvbench2.dat";

using reporter_null = std::string;

namespace
{
class reporters
	: public std::tuple<
		reporter_null
		GATE_RUSAGE(reporter_rusage)
		GATE_PAPI(reporter_papi)
	>
{
public:
	reporters(const char *tag_)
		: std::tuple<
			reporter_null
			GATE_RUSAGE(reporter_rusage)
			GATE_PAPI(reporter_papi)
		>(
			tag_
			GATE_RUSAGE(tag_)
			GATE_PAPI(tag_)
		)
	{}
};

constexpr auto MIN_IO_SIZE = geometry::slot_size(); /* cache line size */
constexpr auto uint64_per_slot = geometry::uint64_per_slot();

/* As of bandwidth testing, we need to support more than one pmem device */
std::vector<std::shared_ptr<Pmem_access>> pmem0;
std::vector<std::shared_ptr<Pmem_access>> pmem1;
std::vector<std::shared_ptr<Pmem_access>> dram0;
std::vector<std::shared_ptr<Pmem_access>> dram1;

void init_dram_local(std::size_t MEMORY_SIZE)
{
  constexpr char dram_tag[] = "DRAM";
  dram0.emplace_back(new numa_local<uint64_t>(MEMORY_SIZE, dram_tag));
  dram1.emplace_back(new numa_local<uint64_t>(MEMORY_SIZE, dram_tag));
}

void init_dram_on_node(std::size_t MEMORY_SIZE, int node)
{
  constexpr char dram_tag[] = "DRAM";
  dram0.emplace_back(new numa_on_node<uint64_t>(MEMORY_SIZE, node, dram_tag));
  dram1.emplace_back(new numa_on_node<uint64_t>(MEMORY_SIZE, node, dram_tag));
}

void init_pmem_emu(std::size_t MEMORY_SIZE, const std::string &)
{
  constexpr char emu_nvdimm_tag[] = "emuNVDIMM";
  pmem0.emplace_back(new numa_local<uint64_t>(MEMORY_SIZE, emu_nvdimm_tag));
  pmem1.emplace_back(new numa_local<uint64_t>(MEMORY_SIZE, emu_nvdimm_tag));
}

void init_pmem_base(std::size_t MEMORY_SIZE, const std::string &dir_)
{
#if PERSIST_POSSIBLE
  pmem0.emplace_back(new Pmem_base(dir_ + "/" + pmem_file_0, MEMORY_SIZE));
  pmem1.emplace_back(new Pmem_base(dir_ + "/" + pmem_file_1, MEMORY_SIZE));
#else
  PLOG("Persist not available; using emulation");
  init_pmem_emu(MEMORY_SIZE, dir_);
#endif
}

void init_pmem_xms(std::size_t MEMORY_SIZE, const std::string &)
{
#if PERSIST_POSSIBLE
  pmem0.emplace_back(new Pmem_xms(MEMORY_SIZE, 0));
  pmem1.emplace_back(new Pmem_xms(MEMORY_SIZE, MEMORY_SIZE /*offset */));
#else
  PLOG("Persist not available; using emulation");
  init_pmem_emu(MEMORY_SIZE, "");
#endif
}

void hist_test(
	bool log2_hist
	, const mem_test<uint64_t> &mt_
	, const cpu_measured &cpu_
	, const linear_histogram_factory<unsigned long> &lhf_
	, void (*linear_test_)(const mem_test<uint64_t> &mt, const cpu_measured &, const linear_histogram_factory<unsigned long> &)
	, void (*log2_test_)(const mem_test<uint64_t> &mt, const cpu_measured &)
)
{
	if ( log2_hist ) {
		if ( log2_test_ ) {
			log2_test_(mt_, cpu_);
		}
	} else {
		if ( linear_test_ ) {
			linear_test_(mt_, cpu_, lhf_);
		}
	}
}

void random_8byte_read(const mem_test<uint64_t> &mt, const cpu_measured &cpu_)
{
  if ( const auto collection_size = write_cycle(mt, sizeof(uint64_t)) )
  {
    cpu_time_t delay;
    {
      reporters r{__func__};
      delay = std::get<0>(random_8byte_read_prepared(mt, collection_size));
    }

    PINF("%s_%s: per op: %f nsec", mt.type(), __func__, cpu_.tsc_to_nsec(delay)/double(collection_size));
  }
}

#define LINEAR_FORMAT "%s_%s: per op: %f nsec linear range step %f nsec capture fraction %f [%s]"

void random_8byte_read_histogram_linear(const mem_test<uint64_t> &mt, const cpu_measured &cpu_, const linear_histogram_factory<unsigned long> &lhf)
{
  if ( const auto collection_size = write_cycle(mt, sizeof(uint64_t)) )
  {
    auto h = lhf.make();

    cpu_time_t delay;

    {
      const auto m = mt.data();
      reporters r{__func__};
      cpu_time_t s = rdtsc();
      const auto start = s;
      auto pos = m[0];
      while ( pos != 0 ) {
        pos = m[pos];
        const auto split = rdtsc();
        h->enter(cpu_.tsc_to_nsec(split - s));
        s = split;
      }
      external_count = pos;
      const auto end = rdtsc();
      h->enter(cpu_.tsc_to_nsec(end - s));
      delay = end - start;
    }

    const auto r = h->captured_mean_range();
    PLOG("histogram mean range %f to %f", r.first, r.second);
    const auto hp = Common::join(",", h->hist().begin(), h->end_non_zero());
    PINF(LINEAR_FORMAT, mt.type(), __func__, cpu_.tsc_to_nsec(delay)/double(collection_size), lhf.incr(), h->capture_fraction(), hp.c_str());
  }
}

#define LOG2_FORMAT "%s_%s: per op: %f nsec log2 histogram first partition at %f nsec [%s]"

void random_8byte_read_histogram_log2(const mem_test<uint64_t> &mt, const cpu_measured &cpu_)
{
  if ( const auto collection_size = write_cycle(mt, sizeof(uint64_t)) )
  {
    Common::histogram_log2<cpu_time_t> h{};

    cpu_time_t delay;

    {
      const auto m = mt.data();
      reporters r{__func__};
      cpu_time_t s = rdtsc();
      const cpu_time_t start = s;
      auto pos = m[0];
      while ( pos != 0 ) {
        pos = m[pos];
        const auto split = rdtsc();
        h.enter(split - s);
        s = split;
      }
      external_count = pos;
      const auto end = rdtsc();
      h.enter(end - s);

      delay = end - start;
    }

    const auto hp = Common::join(",", h.hist().begin(), h.hist().end());
    PINF(LOG2_FORMAT, mt.type(), __func__, cpu_.tsc_to_nsec(delay)/double(collection_size), cpu_.ghz(), hp.c_str());
  }
}

void random_8byte_write(const mem_test<uint64_t> &mt, const cpu_measured &cpu_)
{
  cpu_time_t delay;
  const auto positions = pattern_random{mt.geo()}.generate(sizeof *mt.data());

  {
    reporters r{__func__};
    delay = random_8byte_write_prepared(mt, positions);
  }

  PINF("%s_%s: per op: %f nsec", mt.type(), __func__, cpu_.tsc_to_nsec(delay)/double(positions.size()));
}

void random_8byte_write_histogram_linear(const mem_test<uint64_t> &mt, const cpu_measured &cpu_, const linear_histogram_factory<unsigned long> &lhf)
{
  auto h = lhf.make();
  const auto m = mt.data();
  const auto positions = pattern_random{mt.geo()}.generate(sizeof *m);

  cpu_time_t delay;
  {
    reporters r{__func__};
    cpu_time_t s = rdtsc();
    const cpu_time_t start = s;
    for(auto pos : positions) {
      m[pos] = pos;
      const auto split = rdtsc();
      h->enter(cpu_.tsc_to_nsec(split - s));
      s = split;
    }
    const auto end = rdtsc();
    delay = end - start;
  }

  const auto r = h->captured_mean_range();
  PLOG("histogram mean range %f to %f", r.first, r.second);
  const auto hp = Common::join(",", h->hist().begin(), h->end_non_zero());
  PINF(LINEAR_FORMAT, mt.type(), __func__, cpu_.tsc_to_nsec(delay)/double(positions.size()), lhf.incr(), h->capture_fraction(), hp.c_str());
}

void random_8byte_write_persist(const mem_test<uint64_t> &mt, const cpu_measured &cpu_)
{
  cpu_time_t delay;
  const auto positions = pattern_random{mt.geo()}.generate(sizeof *mt.data());

  {
    reporters r{__func__};
    delay = random_8byte_write_persist_prepared(mt, positions);
  }

  PINF("%s_%s: per op: %f nsec", mt.type(), __func__, cpu_.tsc_to_nsec(delay)/double(positions.size()));
}

void random_8byte_write_persist_histogram_linear(const mem_test<uint64_t> &mt, const cpu_measured &cpu_, const linear_histogram_factory<unsigned long> &lhf)
{
  auto h = lhf.make();
  const auto m = mt.data();
  const auto positions = pattern_random{mt.geo()}.generate(sizeof *m);
  const auto size = positions.size();

  cpu_time_t delay;
  {
    reporters r{__func__};
    cpu_time_t s = rdtsc();
    const cpu_time_t start = s;
    for(auto pos : positions) {
      m[pos] = pos;
      persist(m[pos]);
      const auto split = rdtsc();
      h->enter(cpu_.tsc_to_nsec(split - s));
      s = split;
    }
    const auto end = rdtsc();
    delay = end - start;
  }

  const auto r = h->captured_mean_range();
  PLOG("histogram mean range %f to %f", r.first, r.second);
  const auto hp = Common::join(",", h->hist().begin(), h->end_non_zero());
  PINF(LINEAR_FORMAT, mt.type(), __func__, cpu_.tsc_to_nsec(delay)/double(size), lhf.incr(), h->capture_fraction(), hp.c_str());
}

void random_8byte_write_histogram_log2(const mem_test<uint64_t> &mt, const cpu_measured &cpu_)
{
  Common::histogram_log2<cpu_time_t> h{};
  const auto m = mt.data();
  const auto positions = pattern_random{mt.geo()}.generate(sizeof *m);

  cpu_time_t delay;
  {
    reporters r{__func__};
    cpu_time_t s = rdtsc();
    const auto start = s;
    for(auto pos : positions) {
      m[pos] = pos;
      const cpu_time_t split = rdtsc();
      h.enter(split - s);
      s = split;
    }
    const auto end = rdtsc();
    delay = end - start;
  }

  const auto hp = Common::join(",", h.hist().begin(), h.hist().end());
  PINF(LOG2_FORMAT, mt.type(), __func__, cpu_.tsc_to_nsec(delay)/double(positions.size()), cpu_.ghz(), hp.c_str());
}

void random_8byte_write_persist_histogram_log2(const mem_test<uint64_t> &mt, const cpu_measured &cpu_)
{
  Common::histogram_log2<cpu_time_t> h{};
  const auto m = mt.data();
  const auto positions = pattern_random{mt.geo()}.generate(sizeof *m);

  cpu_time_t delay;
  {
    reporters r{__func__};
    cpu_time_t s = rdtsc();
    const auto start = s;

    for(auto pos : positions) {
      m[pos] = pos;
      persist(m[pos]);
      const auto split = rdtsc();
      h.enter(split - s);
      s = split;
    }
    const auto end = rdtsc();
    delay = end - start;
  }
  const auto hp = Common::join(",", h.hist().begin(), h.hist().end());
  PINF(LOG2_FORMAT, mt.type(), __func__, cpu_.tsc_to_nsec(delay)/double(positions.size()), cpu_.ghz(), hp.c_str());
}

/* TESTS (generic, large access) */

/*
 * Types of persistence. We have three tyes:
 *  persist: mem* functions with persistence through pmem
 *  nodrain: mem* functions through pmem, but without persistence
 *  std: standard mem* functions, not the pmem versions
 *
 * Distinguishing among persist, nodrain, and std at compile time may be overkill,
 * but we intend to avoid the extra dereference in the inner loop. May be overkill
 * for mem operations, but could be if extended to simple integer operations.
 */

/* memcpy and memset tests */

void memget_test(std::shared_ptr<Pmem_access> m_, const cpu_measured &cpu_, const pattern &pat, std::size_t sz_)
{
  std::vector<uint64_t> dst(Common::div_round_up(sz_,sizeof(uint64_t)));
  const mem_test<uint64_t> pm_src{m_, pat.geo()};
  const auto positions = pat.generate(sz_);
  memcpy_init(pm_src, positions, sz_);
  auto r = memget_run(&dst[0], pm_src.data(), sz_, positions.size(), pat.sanity_check());
  PINF("%s_%s_%zubyte_memget: per op: %f nsec", pm_src.type(), pat.name(), sz_, cpu_.tsc_to_nsec(std::get<0>(r))/double(std::get<1>(r)));
}

template <enum persistence P>
  void memset_test(std::shared_ptr<Pmem_access> m, const cpu_measured &cpu_, const pattern &pat_, std::size_t sz_, int fillchar)
  {
    using attrs = OPS<P>; /* persistence attributes */

    const mem_test<uint64_t> pm{m, pat_.geo()};

    const auto positions = pat_.generate(sz_);

    const auto r = memset_run<P>(pm.data(), positions, sz_, fillchar);

    PINF("%s_%s_%zubyte_memset_%s: per op: %f nsec", pm.type(), pat_.name(), sz_, attrs::tag, cpu_.tsc_to_nsec(std::get<0>(r))/double(std::get<1>(r)));
  }

/* TESTS */

template <typename T>
cpu_time_t bitsweep_persist(T *cursor, T *last)
{
  const cpu_time_t start = rdtsc();
  for(; cursor != last; ++cursor) {
    for(unsigned j=std::numeric_limits<T>::digits;j!=0;--j) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
      *cursor |= T(1) << (j-1);
#pragma GCC diagnostic pop
      persist(*cursor); /* byte-level persist */
    }
  }
  return rdtsc() - start;
}

void sweep_clear(uint8_t *data, std::size_t MEMORY_SIZE)
{
  const auto SWEEP_SIZE = std::min(std::size_t(MB(128)), MEMORY_SIZE);
  std::memset(data, 0, SWEEP_SIZE);
}

void test_bitsweep(const mem_test<uint8_t> &m, const cpu_measured &cpu_, std::size_t MEMORY_SIZE)
{
  const auto SWEEP_SIZE = std::min(std::size_t(MB(128)), MEMORY_SIZE);
  const auto dur = bitsweep_persist(m.data(), m.data() + SWEEP_SIZE);

  PINF("%s_bitsweep: per op: %f nsec",
       m.type(), cpu_.tsc_to_nsec(dur) / double(SWEEP_SIZE * CHAR_BIT));
}

} /* namespace */

/* Initialization helpers */

namespace {
/*
 * Compare an Intel (rdtsc) measured time with a C++ (std::chrono::...::now()) measured time.
 * Warn if they are not roughly the same.
 */
  void compare_frequencies(double i_freq, double c_freq)
  {

    if ( 0.05 < std::abs(i_freq-c_freq)/c_freq ) {
      PLOG("Specified clock frequency %f differs from measured std::chrono frequency %f by more than 5%%. Check --clkfreq", i_freq, c_freq);
    }
  }

  void log_pmem_vars()
  {
    static const char *const env_vars_pmem[] {
      "PMEM_AVX"
      , "PMEM_AVX512F"
      , "PMEM_NO_CLFLUSHOPT"
      , "PMEM_NO_CLWB"
      , "PMEM_NO_MOVNT"
      /* Intel CPU model:agnostic: */
      , "PMEM_MOVNT_THRESHOLD"
      /* Common (Intel and AMD): */
      , "PMEM_NO_FLUSH"
      /* Common: */
      , "PMEM_IS_PMEM_FORCE"
    };

    for ( const auto k : env_vars_pmem ) {
      const auto v = std::getenv(k);
      if ( v ) {
        PLOG("%s=%s", k, v);
      }
    }
  }

} /* namespace */

void test_scalar_latency(
	const std::shared_ptr<Pmem_access> &m_
	, const cpu_measured &cpu_
	, const geometry &g_
	, const bool log2_hist_
	, const linear_histogram_factory<unsigned long> &lhf_
)
{
	random_8byte_read(mem_test<uint64_t>{m_, g_}, cpu_);
	hist_test(log2_hist_, mem_test<uint64_t>{m_, g_}, cpu_, lhf_, random_8byte_read_histogram_linear, random_8byte_read_histogram_log2);
	random_8byte_write(mem_test<uint64_t>{m_, g_}, cpu_);
	hist_test(log2_hist_, mem_test<uint64_t>{m_, g_}, cpu_, lhf_, random_8byte_write_histogram_linear, random_8byte_write_histogram_log2);
	random_8byte_write_persist(mem_test<uint64_t>{m_, g_}, cpu_);
	hist_test(log2_hist_, mem_test<uint64_t>{m_, g_}, cpu_, lhf_, random_8byte_write_persist_histogram_linear, random_8byte_write_persist_histogram_log2);
}

class memory_threads_nexus
{
	std::shared_ptr<Pmem_access> _mem;
	cpu_mask_t _af;
public:
	explicit memory_threads_nexus(std::shared_ptr<Pmem_access> mem_, cpu_mask_t af_)
		: _mem{mem_}
		, _af{af_}
	{}
	std::shared_ptr<Pmem_access> mem() const { return _mem; }
	cpu_mask_t affinity() const { return _af; }
};

/* Construct an object asynchronously.
 * Useful when the construtor takes a long time.
 */
template <class T, class ... Args>
	std::future<T> construct_async(Args && ... args)
	{
		return
			std::async(
				std::launch::async
				, [=] () -> T
					{
						return T(args ...);
					}
			);
	}

template <typename LG>
	void bandwidth_8byte(
		const std::vector<memory_threads_nexus> nx_
		, const cpu_measured &cpu_
		, const geometry &g_
	)
	{
		std::vector<std::future<LG>> v_prep;
		std::vector<LG> v_in;
		std::vector<std::future<std::tuple<cpu_time_t, std::size_t>>> v_out;
		cpu_time_t accum_delay = cpu_time_t(0);
		std::size_t accum_ops = 0;
		std::string test_tag = LG::type(8);

		/* initialize tests */
		for ( const auto &nx : nx_ )
		{
			const auto n_cpus = nx.affinity().count();
			/* Apportion memory among the CPUs */
			const auto thread_geometry_size = g_.slot_count()/n_cpus;
			std::size_t thread_geometry_lo = 0;
			for ( auto i : nx.affinity() )
			{
				const auto thread_geometry_hi = thread_geometry_lo + thread_geometry_size;
				const auto g = g_.bound(thread_geometry_lo, thread_geometry_hi);
				v_prep.emplace_back(
					construct_async<LG>(nx.mem(), i, g)
				);
				thread_geometry_lo = thread_geometry_hi;
			}
		}
		/* prepare tests */
		for ( auto &f : v_prep ) { v_in.emplace_back(f.get()); }
		/* start tests */
		{
			reporters r{test_tag.c_str()};
			for ( const auto &lg : v_in )
			{
				v_out.emplace_back(std::async(std::launch::async, &LG::run, lg));
			}

			for ( auto &f: v_out ) {
				const auto rr = f.get();
				accum_delay += std::get<0>(rr);
				accum_ops += std::get<1>(rr);
			}
		}
		PINF("%s_bandwidth_%s: %f GB/sec", nx_.front().mem()->type(), test_tag.c_str(), double(accum_ops*sizeof(uint64_t))/cpu_.tsc_to_nsec(accum_delay)/double(v_out.size()));
	}

template <typename LG, typename ... Args>
	void bandwidth_sized(
		const std::vector<memory_threads_nexus> nx_
		, const cpu_measured &cpu_
		, const geometry &g_
		, std::size_t sz_
		, Args && ... args_
	)
	{
		std::vector<std::future<LG>> v_prep;
		std::vector<LG> v_in;
		std::vector<std::future<std::tuple<cpu_time_t, std::size_t>>> v_out;
		cpu_time_t accum_delay = cpu_time_t(0);
		std::size_t accum_ops = 0;
		std::string test_tag = LG::type(sz_);

		/* initialize tests */
		for ( const auto &nx : nx_ )
		{
			const auto n_cpus = nx.affinity().count();
			/* Apportion memory among the CPUs */
			const auto thread_geometry_size = g_.slot_count()/n_cpus;
			std::size_t thread_geometry_lo = 0;
			for ( auto i : nx.affinity() )
			{
				const auto thread_geometry_hi = thread_geometry_lo + thread_geometry_size;
				const auto g = g_.bound(thread_geometry_lo, thread_geometry_hi);
				const auto pat = pattern_random{g};
				v_prep.emplace_back(
					construct_async<LG>(nx.mem(), i, g, pat, sz_, args_ ...)
				);
				thread_geometry_lo = thread_geometry_hi;
			}
		}
		/* prepare tests */
		for ( auto &f : v_prep ) { v_in.emplace_back(f.get()); }
		/* start tests */
		{
			reporters r{test_tag.c_str()};
			for ( const auto &lg : v_in )
			{
				v_out.emplace_back(std::async(std::launch::async, &LG::run, lg));
			}

			for ( auto &f: v_out ) {
				const auto rr = f.get();
				accum_delay += std::get<0>(rr);
				accum_ops += std::get<1>(rr);
			}
		}
		PINF("%s_bandwidth_%s: %f GB/sec", nx_.front().mem()->type(), test_tag.c_str(), double(accum_ops*sz_)/cpu_.tsc_to_nsec(accum_delay)/double(v_out.size()));
	}

void test_bandwidth(
	const std::vector<memory_threads_nexus> n_
	, const cpu_measured &cpu_
	, const geometry &g_
	, const std::vector<std::size_t> &sizes_
	, int fillchar_
)
{
	bandwidth_8byte<load_generator_8byte_read_random>(n_, cpu_, g_);
	bandwidth_8byte<load_generator_8byte_write_random_persist>(n_, cpu_, g_);

	for ( auto sz : sizes_ )
	{
		bandwidth_sized<load_generator_memget>(n_, cpu_, g_, sz);
		bandwidth_sized<load_generator_memset_persist>(n_, cpu_, g_, sz, fillchar_);
	}
}

cpu_mask_t string_to_cpumask(const std::string s)
{
	cpu_mask_t m;
	const auto status = string_to_mask(s, m);
	if ( status != S_OK ) {
		throw std::domain_error{"Cannot interpret cpu mask '" + s + "'"};
	}
	return m;
}

/* Similar to Python zip function */
template <typename T, typename U, typename PAIR>
	PAIR zip(T first1, T last1, U first2, U last2, PAIR p)
	{
		while ( first1 != last1 && first2 != last2 )
		{
			*p++ = typename PAIR::container_type::value_type{*first1++, *first2++};
		}
		return p;
	}

void set_affinity(const cpu_mask_t affinity)
{
  if ( affinity.count() ) {
    auto affinity_spec = cpumask_to_string(affinity);
    if ( 0 != set_cpu_affinity_mask(affinity) ) {
      const auto e = errno;
      throw std::system_error{std::error_code{e, std::system_category()}, std::string{"Failed to set affinity "} + affinity_spec};
    }
    PLOG("CPU affinity: %s", affinity_spec.c_str());
  } else {
    PLOG("CPU affinity: none");
  }
}

void test_latency_dram(
  const std::vector<std::shared_ptr<Pmem_access>> &dram0_
  , const std::vector<std::shared_ptr<Pmem_access>> &dram1_
  , const cpu_measured &cpu_
  , const geometry &g_
  , const std::vector<std::size_t> &sizes_
  , const int fillchar_
  , const linear_histogram_factory<unsigned long> &lhf_
  , const bool log2_hist_
)
{
  test_scalar_latency(dram0_.front(), cpu_, g_, log2_hist_, lhf_);
  /* The byte-copy tests have these aspects:
   *  1. memory type (DRAM or NVDIMM)
   *  2. function type (memcpy or memset)
   *  3. pattern (random or sequential)
   *  4. persistence (nodrain or persist)
   *  5. size (8, 64, 1024, or 4096 bytes)
   *
   * Each aspect should be controlled by a class or a tempate. So far,
   *  memory type:   controlled by a class, in that the emulation class for NVDIMM
   *                 is the same as the class of DRAM.
   *  function type: sequential code, not a class or template pr value
   *  pattern:       a subclass of pattern
   *  persistence:   a template specialization of OPS
   *  size:          a value of size_t
   */

  {
    const auto p = pattern_random{g_};

    for ( auto sz : sizes_ ) {
      memcpy_test<persistence::STD>(dram0_.front(), dram1_.front(), cpu_, p, sz);
      memcpy_test<persistence::PERSIST>(dram0.front(), dram1.front(), cpu_, p, sz);
    }

    for ( auto sz : sizes_ ) {
      memset_test<persistence::STD>(dram0_.front(), cpu_, p, sz, fillchar_);
      memset_test<persistence::PERSIST>(dram0_.front(), cpu_, p, sz, fillchar_);
    }

    for ( auto sz : sizes_ ) {
      memget_test(dram0_.front(), cpu_, p, sz);
    }
  }

  {
    const auto p = pattern_sequential{g_};

    for ( auto sz : sizes_ ) {
      memcpy_test<persistence::STD>(dram0_.front(), dram1_.front(), cpu_, p, sz);
      memcpy_test<persistence::PERSIST>(dram0_.front(), dram1_.front(), cpu_, p, sz);
    }

    for ( auto sz : sizes_ ) {
      memset_test<persistence::STD>(dram0_.front(), cpu_, p, sz, fillchar_);
      memset_test<persistence::PERSIST>(dram0_.front(), cpu_, p, sz, fillchar_);
    }

    for ( auto sz : sizes_ ) {
      memget_test(dram0_.front(), cpu_, p, sz);
    }
  }
}

void test_latency_nvdimm(
  const std::vector<std::shared_ptr<Pmem_access>> &pmem0_
  , const std::vector<std::shared_ptr<Pmem_access>> &pmem1_
  , const cpu_measured &cpu_
  , const geometry &g_
  , const std::vector<std::size_t> &sizes_
  , const int fillchar_
  , const linear_histogram_factory<unsigned long> &lhf_
  , const bool log2_hist_
)
{
  test_scalar_latency(pmem0.front(), cpu_, g_, log2_hist_, lhf_);

  {
    const auto p = pattern_random{g_};

    for ( auto sz : sizes_ ) {
      memcpy_test<persistence::NODRAIN>(pmem0_.front(), pmem1_.front(), cpu_, p, sz);
      memcpy_test<persistence::PERSIST>(pmem0_.front(), pmem1_.front(), cpu_, p, sz);
    }

    for ( auto sz : sizes_ ) {
      memset_test<persistence::NODRAIN>(pmem0_.front(), cpu_, p, sz, fillchar_);
      memset_test<persistence::PERSIST>(pmem0_.front(), cpu_, p, sz, fillchar_);
    }

    for ( auto sz : sizes_ ) {
      memget_test(pmem0.front(), cpu_, p, sz);
    }
  }

  {
    const auto p = pattern_sequential{g_};

    for ( auto sz : sizes_ ) {
      memcpy_test<persistence::NODRAIN>(pmem0_.front(), pmem1_.front(), cpu_, p, sz);
      memcpy_test<persistence::PERSIST>(pmem0_.front(), pmem1_.front(), cpu_, p, sz);
    }

    for ( auto sz : sizes_ ) {
      memset_test<persistence::NODRAIN>(pmem0_.front(), cpu_, p, sz, fillchar_);
      memset_test<persistence::PERSIST>(pmem0_.front(), cpu_, p, sz, fillchar_);
    }

    for ( auto sz : sizes_ ) {
      memget_test(pmem0_.front(), cpu_, p, sz);
    }
  }
}

void test_bandwidth(
  std::vector<std::shared_ptr<Pmem_access>> &mem0_
  , const cpu_measured &cpu_
  , const geometry &g_
  , const std::vector<std::size_t> &sizes_
  , const int fillchar_
  , const std::vector<std::string> &aff_specs_
)
{
  std::vector<cpu_mask_t> affs;
  std::transform(aff_specs_.begin(), aff_specs_.end(), std::back_inserter(affs), string_to_cpumask);
  if ( mem0_.size() != affs.size() )
  {
    throw std::domain_error{
      std::string{"Count mismatch. Cannot associate "}
        + std::to_string(mem0_.size())
        + " memories with "
        + std::to_string(affs.size()) + " affinity specifications"};
  }
  std::vector<memory_threads_nexus> nv;
  zip(mem0_.begin(), mem0_.end(), affs.begin(), affs.end(), std::back_inserter(nv));
  for ( const auto &nx : nv )
  {
    std::string aff_str = cpumask_to_string(nx.affinity());
    PLOG("CPU(s) %s -> %s at node %s", aff_str.c_str(), nx.mem()->type(), nx.mem()->id().c_str());
  }

  test_bandwidth(nv, cpu_, g_, sizes_, fillchar_);
}

std::size_t round_up_to_pow2(std::size_t x)
{
  std::size_t y = 1;
  while ( y < x )
  {
    y <<= 1;
  }
  return y;
}


using namespace std;

/* environment variables tested in libpmem:
 * Intel, CPU model-dependent:
 *  PMEM_AVX: set to 1, or else avx will not be used (even if supported)
 *  PMEM_AVX512F: set to 1, or else avx512f will not be used (even if supported)
 *  PMEM_NO_CLFLUSHOPT: set to 1 will disable clflushopt (but clwb may still be used)
 *  PMEM_NO_CLWB: set to 1 will disable clwb (but clflushopt may still be used)
 *  PMEM_NO_MOVNT: set to 1 will disable every flavor of movnt (sse2, avx, and avx512f)
 * Intel CPU model:agnostic:
 *  PMEM_MOVNT_THRESHOLD: the integral memcpy/memmove size below which movnt will not be used
 * Common (Intel and AMD)
 *  PMEM_NO_FLUSH: set to 1 to disable cache flush
 * Common
 *  PMEM_IS_PMEM_FORCE: 0 to pretend that memory ranges are never pmem
 *  PMEM_IS_PMEM_FORCE: 1 to pretend that memory ranges are always pmem
 */

int main(int argc, char * argv[])
{
  namespace po = boost::program_options;
  po::options_description desc("Options");

  constexpr auto po_help = "help";
  constexpr auto po_dram = "dram";
  constexpr auto po_nvdimm = "nvdimm";
  constexpr auto po_nv_dir = "nv-dir";
  constexpr auto po_bitsweep = "bitsweep";
  constexpr auto po_clkfreq = "clkfreq";
  constexpr auto po_cpu_affinity = "cpu-affinity";
#if 0
  constexpr auto po_numa_cpu_aff = "numa-cpu-affinity";
#endif
  constexpr auto po_numa_dram_aff = "numa-dram-affinity";
  constexpr auto po_hist_type = "histogram-type";
  constexpr auto po_hist_size = "histogram-size";
  constexpr auto po_hist_incr = "histogram-incr";
  constexpr auto po_mtype = "mtype";
  constexpr auto po_kb = "kb";
  constexpr auto po_mb = "mb";
  constexpr auto po_gb = "gb";
  constexpr auto po_bw = "bandwidth";
  constexpr auto po_sanity_checks = "sanity-checks";
  constexpr auto po_fillchar = "fillchar";
  constexpr auto po_sizes = "sizes";
  constexpr auto po_pagesize = "pagesize";

  try {

    /*
     * Use the time taken by the initialization code to
     * to compare rdtsc-measured time to std::chrono-measured time.
     */

    const auto c_t0 = std::chrono::high_resolution_clock::now();
    const auto i_t0 = rdtsc();

    desc.add_options()
      (po_help, "Show help message")
      (po_dram, "Run DRAM tests.")
      (po_nvdimm, "Run NVDIMM tests.")
      (po_nv_dir, po::value<std::vector<std::string>>(), "Directories for mapped NVRAM")
      (po_bitsweep, "Run bitsweep tests (left to right bit set)")
      (po_clkfreq, po::value<double>(), "Clock frequency in GHz")
      (po_cpu_affinity, po::value<std::vector<std::string>>(), "CPU affinity")
#if 0
      (po_numa_cpu_aff, po::value<int>(), "CPU NUMA node affinity")
#endif
      (po_numa_dram_aff, po::value<std::vector<int>>(), "DRAM NUMA node affinity")
      (po_hist_type, po::value<std::string>()->default_value("linear"), "histogram type (linear or log2)")
      (po_hist_size, po::value<unsigned>()->default_value(100U), "histogram size, if linear")
      (po_hist_incr, po::value<double>()->default_value(1.0), "histogram increment in nsec, if linear")
      (po_mtype, po::value<std::string>()->default_value("dax"), "dax (pmem), xms or emu (emulated) memory access")
      (po_kb, po::value<std::size_t>(), "Set memory size in kibibytes")
      (po_mb, po::value<std::size_t>(), "Set memory size in mebibytes")
      (po_gb, po::value<std::size_t>(), "Set memory size in gibibytes")
      (po_bw, "Run bandwidth test")
      (po_sanity_checks, "Run sanity checks")
      (po_fillchar, po::value<char>()->default_value('X'), "memset fill character")
      (po_sizes, po::value<std::vector<std::size_t>>(), "memget/cpy/set sizes")
      (po_pagesize, po::value<std::size_t>()->default_value(1U<<21U), "presumed pagesize")
      ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc),  vm);
    po::notify(vm);

    std::vector<std::string> pmem_dirs =
      vm.count(po_nv_dir)==0
      ? std::vector<std::string>(1,"/mnt/mem0")
      : vm[po_nv_dir].as<std::vector<string>>()
    ;

    using init_pmem_t = void (*)(std::size_t memory_size, const std::string &);

    static std::map<std::string, init_pmem_t> m {
      { "dax", init_pmem_base, },
      { "xms", init_pmem_xms, },
      { "emu", init_pmem_emu, },
    };

    const auto mem_type = vm[po_mtype].as<string>();
    const auto it = m.find(mem_type);
    if ( it == m.end() ) {
      throw std::domain_error{std::string{"Cannot interpret mtype '"} + mem_type + "', expected 'dax' or 'xms' or 'emu'"};
    }
    const init_pmem_t init_pmem = it->second;

    if (vm.count(po_help)) {
      cout << desc << "\n";
      return 1;
    }

    bool sanity_check = vm.count(po_sanity_checks) != 0;

    const cpu_mask_t affinity =
      vm.count(po_cpu_affinity) != 0
      ? string_to_cpumask(vm[po_cpu_affinity].as<std::vector<string>>()[0])
      : cpu_mask_t{}
      ;
#if 0
    if ( vm.count(po_cpu_affinity) != 0 ) {
      const auto affinity_spec = vm[po_cpu_affinity].as<std::vector<string>>()[0];
      affinity = string_to_cpumask(affinity_spec);
    }
#endif
    std::vector<int> dram_node;
    if ( vm.count(po_numa_dram_aff) != 0 ) {
      dram_node = vm[po_numa_dram_aff].as<std::vector<int>>();
    }

    const auto histogram_type = vm[po_hist_type].as<string>();

    const bool log2_hist = histogram_type == "log2";

    if ( histogram_type == "linear" ) {
    }
    else if ( histogram_type == "log2" ) {
    } else {
      throw std::domain_error{std::string{"Cannot interpret histogram-type '"} + histogram_type + "', expected 'linear' or 'log2'"};
    }

    linear_histogram_factory<unsigned long> lhf{
	vm.count(po_hist_size) != 0 ? vm[po_hist_size].as<unsigned>() : 100
	, vm.count(po_hist_incr) != 0 ? vm[po_hist_incr].as<double>() : 1.0
    };

    if ( 1 < vm.count(po_kb) + vm.count(po_mb) + vm.count(po_gb) ) {
      throw std::domain_error{std::string{"Options 'kb', 'mb' and 'gb' are mutually exclusive"}};
    }

    size_t MEMORY_SIZE = MB(128);
    if ( vm.count(po_kb) != 0 ) {
      MEMORY_SIZE = KB(vm[po_kb].as<std::size_t>());
    }
    if ( vm.count(po_mb) != 0 ) {
      MEMORY_SIZE = MB(vm[po_mb].as<std::size_t>());
    }
    if ( vm.count(po_gb) != 0 ) {
      MEMORY_SIZE = GB(vm[po_gb].as<std::size_t>());
    }
    PLOG("Memory size: %zu MiB", MEMORY_SIZE/MB(1));

    double CYCLES_PER_NSEC = 0;

    if ( vm.count(po_clkfreq) != 0 ) {
      CYCLES_PER_NSEC = vm[po_clkfreq].as<double>();
    }

    const auto pagesize = round_up_to_pow2(vm[po_pagesize].as<std::size_t>());

    const size_t SLOTS =
	(MEMORY_SIZE / MIN_IO_SIZE)
	; /* 512 gives max write of 8K */
    const auto slots_per_page = pagesize/MIN_IO_SIZE;

    const geometry g { SLOTS, slots_per_page, sanity_check };

    PLOG("slot count %zu", g.slot_count());

    if(vm.count(po_nvdimm) > 0) {
      log_pmem_vars();
    }

    {
      const auto i_dur = rdtsc() - i_t0;
      const auto c_dur = std::chrono::high_resolution_clock::now() - c_t0;

      const double measured_clock_frequency = double(i_dur)/std::chrono::duration<double>(c_dur).count();
      PLOG("measured clock frequency: %f GHz", measured_clock_frequency / 1e9);

      if ( CYCLES_PER_NSEC == 0 )
      {
        CYCLES_PER_NSEC = measured_clock_frequency;
      }
      else
      {
        PLOG("specified clock frequency: %f GHz", CYCLES_PER_NSEC);
        compare_frequencies(CYCLES_PER_NSEC, measured_clock_frequency);
      }
    }

    const cpu_measured cpu{CYCLES_PER_NSEC};

    const bool run_bandwidth = vm.count(po_bw) != 0;
    const bool run_bitsweep = vm.count(po_bitsweep) != 0;
    /* default, latency test */
    const bool run_latency = ! run_bandwidth && ! run_bitsweep;

    const int fillchar = vm[po_fillchar].as<char>();

    std::vector<std::size_t> sizes = vm.count(po_sizes) ? vm[po_sizes].as<std::vector<std::size_t>>() : std::vector<std::size_t>{ 8, 64, 1024, 4096 };

    if(vm.count(po_dram) > 0) {
      PINF("Running DRAM tests...");

      if ( dram_node.size() != 0 ) {
        for ( auto node : dram_node ) {
          init_dram_on_node(MEMORY_SIZE, node);
        }
      } else {
          init_dram_local(MEMORY_SIZE);
      }

      if ( run_bandwidth ) {
        test_bandwidth(dram0, cpu, g, sizes, fillchar, vm[po_cpu_affinity].as<std::vector<string>>());
      }
      if ( run_bitsweep ) {
        set_affinity(affinity);
        test_bitsweep(mem_test<uint8_t>{dram0.front(), g}, cpu, MEMORY_SIZE);
      }
      if ( run_latency ) {
        set_affinity(affinity);
        test_latency_dram(dram0, dram1, cpu, g, sizes, fillchar, lhf, log2_hist);
      }
    }

    if( vm.count(po_nvdimm) ) {
      PINF("Running NVDIMM tests...");
      for ( auto pmem_dir : pmem_dirs ) {
        init_pmem(MEMORY_SIZE, pmem_dir);
      }

      if ( run_bandwidth ) {
        test_bandwidth(pmem0, cpu, g, sizes, fillchar, vm[po_cpu_affinity].as<std::vector<string>>());
      }
      if ( run_bitsweep ) {
        set_affinity(affinity);
        test_bitsweep(mem_test<uint8_t>{pmem0.front(), g, sweep_clear}, cpu, MEMORY_SIZE);
      }
      if ( run_latency ) {
        set_affinity(affinity);
        test_latency_nvdimm(pmem0, pmem1, cpu, g, sizes, fillchar, lhf, log2_hist);
      }
    }
  }
  catch (std::exception &e) {
    cout << e.what() << "\n";
    return -1;
  }
  catch (Exception &e) {
    cout << e.cause() << "\n";
    return -1;
  }
  catch(...) {
    cout << desc << "\n";
    return -1;
  }

  return 0;
}
