/*
   Copyright [2017-2019] [IBM Corporation]
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/


#ifndef _FABRIC_MEMORY_CONTROL_H_
#define _FABRIC_MEMORY_CONTROL_H_

#include <api/fabric_itf.h> /* component::IFabric_connection */

#include <cstdint> /* uint64_t */
#include <map>
#include <memory> /* shared_ptr */
#include <mutex>
#include <vector>

struct fi_info;
struct fid_domain;
struct fid_mr;
class Fabric;

struct mr_and_address;

struct ru_flt_counter
{
private:
  bool _report;
  long _ru_flt_start;
public:
  ru_flt_counter(bool report);
  ~ru_flt_counter();
};

class Fabric_memory_control
  : public component::IFabric_memory_control
{
  using byte_span = common::byte_span;
  Fabric &_fabric;
  std::shared_ptr<::fi_info> _domain_info;
  std::shared_ptr<::fid_domain> _domain;
  std::mutex _m; /* protects _mr_addr_to_desc, _mr_desc_to_addr */
  /*
   * Map of [starts of] registered memory regions to mr_and_address objects.
   * The map is maintained because no other layer provides fi_mr values for
   * the addresses in an iovec.
   */
  using map_addr_to_mra = std::multimap<const void *, std::unique_ptr<mr_and_address>>;
  map_addr_to_mra _mr_addr_to_mra;
  bool _paging_test;
  ru_flt_counter _fault_counter;

  /*
   * @throw fabric_runtime_error : std::runtime_error : ::fi_mr_reg fail
   */
  ::fid_mr *make_fid_mr_reg_ptr(
    const_byte_span buf
    , std::uint64_t access
    , std::uint64_t key
    , std::uint64_t flags
  ) const;

  ::fid_mr *covering_mr(byte_span v);

public:
  /*
   * @throw fabric_bad_alloc : std::bad_alloc - out of memory
   * @throw fabric_runtime_error : std::runtime_error : ::fi_domain fail
   */
  explicit Fabric_memory_control(
    Fabric &fabric
    , ::fi_info &info
  );

  ~Fabric_memory_control();

  Fabric &fabric() const { return _fabric; }
  ::fi_info &domain_info() const { return *_domain_info; }
  ::fid_domain &domain() const { return *_domain; }

  /* BEGIN component::IFabric_memory_control */
  /**
   * @contig_addr - the address of memory to be registered for RDMA. Restrictions
   * in "man fi_verbs" apply: the memory must be page aligned. The ibverbs layer
   * will execute an madvise(MADV_DONTFORK) syscall against the region. Any error
   * returned from that syscal will cause the register_memory function to fail.
   *
   * @throw std::range_error - address already registered
   * @throw std::logic_error - inconsistent memory address tables
   * @throw fabric_runtime_error : std::runtime_error : ::fi_mr_reg fail
   */
  memory_region_t register_memory(const_byte_span contig_addr, std::uint64_t key, std::uint64_t flags) override;
  /**
   * @throw std::range_error - address not registered
   * @throw std::logic_error - inconsistent memory address tables
   */
  void deregister_memory(const memory_region_t memory_region) override;
  std::uint64_t get_memory_remote_key(const memory_region_t memory_region) const noexcept override;
  void *get_memory_descriptor(const memory_region_t memory_region) const noexcept override;
  /* END component::IFabric_memory_control */

  std::vector<void *> populated_desc(gsl::span<const ::iovec> buffers);
};

#endif
