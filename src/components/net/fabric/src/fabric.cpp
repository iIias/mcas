/*
   Copyright [2017-2021] [IBM Corporation]
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


/*
 * Authors:
 *
 */

#include "fabric.h"

#include "addrinfo.h" /* getaddrinfo_ptr */
#include "fabric_check.h" /* FI_CHECK_ERR */
#include "fabric_client.h"
#include "fabric_client_grouped.h"
#include "fabric_json.h"
#include "fabric_runtime_error.h"
#include "fabric_server_factory.h"
#include "fabric_server_grouped_factory.h"
#include "fabric_str.h" /* tostr */
#include "fabric_util.h"
#include "hints.h"
#include "system_fail.h"

#include "rdma-fabric.h" /* fi_info, FI_VERSION */
#include "rdma-fi_endpoint.h" /* fi_ep_bind, fi_pep_bind */

#include <netdb.h> /* addrinfo */
#include <netinet/in.h> /* sockaddr_in */
#include <common/env.h> /* env_value */
#include <common/pointer_cast.h>
#include <sys/select.h> /* pselect */

#include <chrono> /* seconds */
#include <cstdlib> /* getenv */
#include <iostream> /* cerr */
#include <system_error> /* system_error */
#include <thread> /* sleep_for */
#include <utility> /* tuple */
#include <vector>

/**
 * Fabric/RDMA-based network component
 *
 */

/**
 * Open a fabric provider instance
 *
 * @param json_configuration Configuration string in JSON
 * form. e.g. {
 *   "caps":["FI_MSG","FI_RMA"],
 *   "fabric_attr": { "name": "10.0.0.1/8", "prov_name" : "verbs" },
 *   "bootstrap_addr":"10.0.0.1:9999" }
 * @return
 *
 * caps:
 * name: same format as fi_fabric_attr::name
 * prov_name: same format as fi_fabric_attr::prov_name
 */

namespace
{
  ::fi_eq_attr &eq_attr_init(::fi_eq_attr &attr_)
  {
    attr_.size = 10;
    /*
     * Defaults to FI_WAIT_NONE, in which case we may not call fi_eq_sread.
     * (We would rely on the verbs provider to configure the eq wait type.)
     */
    attr_.wait_obj = FI_WAIT_FD;
    return attr_;
  }

#if 0
  inline void info_dump(::fi_info *f)
  {
    unsigned ct = 0;
    for ( ; f; f = f->next )
    {
      std::cerr << "Info " << ct << " " << tostr(*f) << "\n";
      ++ct;
    }
  }
#endif

  std::shared_ptr<::fid_fabric> make_fid_fabric(::fi_fabric_attr &attr_, void *context_)
  try
  {
    ::fid_fabric *f(nullptr);
    CHECK_FI_ERR(::fi_fabric(&attr_, &f, context_));
    FABRIC_TRACE_FID(f);
    return fid_ptr(f);
  }
  catch ( const fabric_runtime_error &e )
  {
    throw e.add(tostr(attr_));
  }

  std::shared_ptr<::fi_info> make_fi_info(
    std::uint32_t version_
    , const char *node_
    , const char *service_
    , const ::fi_info *hints_
  )
  try
  {
    ::fi_info *f;
    CHECK_FI_ERR(::fi_getinfo(version_, node_, service_, 0, hints_, &f));
    return std::shared_ptr<::fi_info>(f,::fi_freeinfo);
  }
  catch ( const fabric_runtime_error &e_ )
  {
    throw e_.add(std::string(node_ ? node_ : "(no node)") + " " + std::string(service_ ? service_  : "(no service)") + " " + std::string(hints_ ? tostr(*hints_) : "(no hints)"));
  }

  std::shared_ptr<::fi_info> make_fi_info(const ::fi_info &hints)
  {
    return make_fi_info(FI_VERSION(FI_MAJOR_VERSION,FI_MINOR_VERSION), nullptr, nullptr, &hints);
  }

  std::shared_ptr<::fi_info> make_fi_info(const common::string_view json_configuration)
  {
    auto h0 = hints(parse_info(json_configuration));
    using p_to_mr = std::tuple<std::string, int>;
    std::vector<p_to_mr> provider_spec {
      p_to_mr{ "verbs", FI_MR_LOCAL | FI_MR_VIRT_ADDR | FI_MR_ALLOCATED | FI_MR_PROV_KEY }
      /* Contrary to "man fi_domain", FI_MR_BASIC is not the same as a collection of other mode bits. */
      , p_to_mr{ "sockets" , FI_MR_BASIC }
    };

    std::exception_ptr last_exception{};
    for ( auto &spec : provider_spec )
    {
      if ( ! h0.prov_name() || h0.prov_name() == std::get<0>(spec) )
      {
        auto h = hints(make_fi_infodup(h0.data(), "verbs/socket trial"));
        try
        {
          char *n = &std::get<0>(spec)[0];
          return make_fi_info(h.mode(FI_CONTEXT | FI_CONTEXT2).prov_name(n).mr_mode(std::get<1>(spec)).data());
        }
        catch ( fabric_runtime_error &e )
        {
          last_exception = std::current_exception();
        }
      }
    }
    if ( last_exception )
    {
      try
      {
        std::rethrow_exception(last_exception);
      }
      catch ( fabric_runtime_error &e )
      {
        e.add(json_configuration);
        throw;
      }
    }
    throw std::domain_error("provider neither 'sockets' nor 'verbs'");
  }
}

std::ostream &operator<<(std::ostream &o_, const env_replace &e_)
{
	return o_ << e_.key() << "=" << e_.value();
}

Fabric::Fabric(const common::string_view json_configuration_)
	/* libfabric 1.9.0 adds a "mr_cache_monitor" which hooks into various dl calls
	 * concerning memory allocation, including dl_open. The hooking mechanism does
	 * not seem to be thread-safe, meaning that dl_open calls in other threads may
	 * fail/segfault. Disable the cache monitor.
	 */
  : _env_mr_cache_monitor("FI_MR_CACHE_MONITOR", "disabled")
  , _env_use_odp("FI_VERBS_USE_ODP", common::env_value<bool>("USE_ODP", true) ? "true" : "false")
  , _info(make_fi_info(json_configuration_))
  , _fabric(make_fid_fabric(*_info->fabric_attr, this))
  , _eq_attr{}
  , _eq(make_fid_eq(eq_attr_init(_eq_attr), this))
  , _fd()
  , _m_eq_dispatch_pep{}
  , _eq_dispatch_pep{}
  , _m_eq_dispatch_aep{}
  , _eq_dispatch_aep{}
{
  CHECK_FI_ERR(::fi_control(&_eq->fid, FI_GETWAIT, &_fd));
}

namespace
{
  std::uint32_t listen_addr(std::uint16_t port_ )
  {
    auto server_addr_env = "FABRIC_SERVER_ADDR";
    if ( auto addr_str = std::getenv(server_addr_env) )
    {
      auto results = getaddrinfo_ptr(addr_str, port_);
      if ( auto rp = results.get() )
      {
        auto addr_num = ntohl(common::pointer_cast<sockaddr_in>(rp->ai_addr)->sin_addr.s_addr);
        std::cerr << "fabric_server_factory (specified by " << server_addr_env << "=" << addr_str
          << ") listens on "
          << (addr_num >> 24 & 0xff) << "."
          << (addr_num >> 16 & 0xff) << "."
          << (addr_num >> 8 & 0xff) << "."
          << (addr_num & 0xff) << ":" << port_ << "\n";
        return addr_num;
      }
    }
    std::cerr << "fabric_server_factory (not specified by " << server_addr_env << ") listens on *:" << port_ << "\n";
    return INADDR_ANY;
  }
}

namespace
{
  const std::map<const std::string, std::uint16_t> default_port
  {
    { "verbs", 11911 },
    { "sockets", 11921 },
  };
}

std::uint16_t Fabric::choose_port(std::uint16_t port_)
{
  if ( port_ == 0 )
  {
    const auto it = default_port.find(prov_name());
    port_ = it == default_port.end()
      ? 11901
      : it->second
      ;
  }
  return port_;
}

component::IFabric_server_factory * Fabric::open_server_factory(const common::string_view json_configuration_, std::uint16_t control_port_)
{
  _info = parse_info(json_configuration_, _info);
  control_port_ = choose_port(control_port_);
  return new Fabric_server_factory(*this, *this, *_info, listen_addr(control_port_), control_port_);
}

component::IFabric_server_grouped_factory * Fabric::open_server_grouped_factory(const common::string_view json_configuration_, std::uint16_t control_port_)
{
  _info = parse_info(json_configuration_, _info);
  return new Fabric_server_grouped_factory(*this, *this, *_info, listen_addr(control_port_), control_port_);
}

void Fabric::readerr_eq()
{
  ::fi_eq_err_entry entry{};
  auto flags = 0U;
  CHECK_FI_EQ(::fi_eq_readerr(&*_eq, &entry, flags), sizeof entry);

  {
    std::lock_guard<std::mutex> g{_m_eq_dispatch_pep};
    auto p = _eq_dispatch_pep.find(entry.fid);
    if ( p != _eq_dispatch_pep.end() )
    {
      p->second->err(&*_eq, entry);
    }
  }
  {
    std::lock_guard<std::mutex> g{_m_eq_dispatch_aep};
    auto p = _eq_dispatch_aep.find(entry.fid);
    if ( p != _eq_dispatch_aep.end() )
    {
      p->second->err(&*_eq, entry);
    }
  }
}

int Fabric::trywait(::fid **fids_, std::size_t count_) const
{
  /* NOTE: the man page and header file disagree on the signature to fi_trywait.
   * man page says count is size_t; header file says int.
   * NOTE: the man page example for fi_trywait does not include an fds for write,
   *  ignoring this statetment in fi_cq:
   *    "a provider may signal an FD wait object by marking it as readable, writable, or with an error."
   * NOTE: the man page example for fi_trywait is missing the fabric parameter.
   */
  return ::fi_trywait(&*_fabric, fids_, int(count_));
}

void Fabric::wait_eq()
{
  ::fid_t f[1] = { &_eq->fid };
  /* NOTE: although trywait is supposed to fail if there is the queue is non-empty,
   * the pselect seems to time out. The sugests that the indication is edge-riggered
   * and has been missed, and that fi_trywait did not tell us that.
   */
  if ( trywait(f, 1) == FI_SUCCESS )
  {
    int fd;
    CHECK_FI_ERR(::fi_control(&_eq->fid, FI_GETWAIT, &fd));
    fd_set fds_read;
    fd_set fds_write;
    fd_set fds_except;
    FD_ZERO(&fds_read);
    FD_SET(fd, &fds_read);
    FD_ZERO(&fds_write);
    FD_SET(fd, &fds_write);
    FD_ZERO(&fds_except);
    FD_SET(fd, &fds_except);
    struct timespec ts {
      0 /* seconds */
      , 1000000 /* nanoseconds */
    };

    auto ready = ::pselect(fd+1, &fds_read, &fds_write, &fds_except, &ts, nullptr);
    if ( -1 == ready )
    {
      switch ( auto e = errno )
      {
      case EINTR:
        break;
      default:
        system_fail(e, __func__);
      }
    }
  }
}

/*
 * @throw fabric_bad_alloc : std::bad_alloc - libfabric out of memory
 * @throw std::system_error - writing event pipe (normal callback)
 * @throw std::system_error - writing event pipe (readerr_eq)
 */
void Fabric::read_eq()
try
{
  ::fi_eq_cm_entry entry;
  std::uint32_t event = 0;
  auto flags = 0U;
  auto ct = ::fi_eq_read(&*_eq, &event, &entry, sizeof entry, flags);
  if ( ct < 0 )
  {
    try
    {
      switch ( auto e = unsigned(-ct) )
      {
      case FI_EAVAIL:
        readerr_eq();
        break;
      case FI_EAGAIN:
        break;
      default:
        std::cerr << __func__ << ": fabric error " << e << " (" << ::fi_strerror(int(e)) << ")\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
#if 0
        throw fabric_runtime_error(e, __FILE__, __LINE__);
#endif
        break;
      }
    }
    catch ( const std::exception &e )
    {
      std::cerr << __func__ << " (non-error path): exception: " << e.what() << "\n";
      throw;
    }
  }
  else
  {
    bool found = false;
#if 0
    std::cerr << "read_eq: fid " << entry.fid << " event " << get_event_name(event) << "\n";
#endif
    try
    {
      std::unique_lock<std::mutex> g{_m_eq_dispatch_pep};
      auto p = _eq_dispatch_pep.find(entry.fid);
      if ( p != _eq_dispatch_pep.end() )
      {
        auto d = p->second;
        d->cb(event, entry);
        found = true;
      }
    }
    catch ( const std::bad_alloc &e )
    {
      std::cerr << __func__ << " (aep event path): bad_alloc: " << e.what() << "\n";
      throw;
    }
    catch ( const std::exception &e )
    {
      std::cerr << __func__ << " (aep event path): exception: " << e.what() << "\n";
      throw;
    }

    if ( ! found )
    try
    {
      std::unique_lock<std::mutex> g{_m_eq_dispatch_aep};
      auto p = _eq_dispatch_aep.find(entry.fid);
      if ( p != _eq_dispatch_aep.end() )
      {
        auto d = p->second;
        d->cb(event, entry);
        found = true;
      }
    }
    catch ( const std::bad_alloc &e )
    {
      std::cerr << __func__ << " (pep event path): bad_alloc: " << e.what() << "\n";
      throw;
    }
    catch ( const std::exception &e )
    {
      std::cerr << __func__ << " (pep event path): exception: " << e.what() << "\n";
      throw;
    }
  }
}
catch ( const std::bad_alloc &e )
{
  std::cerr << __func__ << ": bad_alloc: " << e.what() << "\n";
  throw;
}
catch ( const std::exception &e )
{
  std::cerr << __func__ << ": exception: " << e.what() << "\n";
  throw;
}

namespace
{
  std::string while_in(const common::string_view where)
  {
    return " (while in " + std::string(where) + ")";
  }
}

component::IFabric_endpoint_unconnected_client * Fabric::make_endpoint(const common::string_view json_configuration_, common::string_view remote_endpoint_, std::uint16_t port_)
try
{
  _info = parse_info(json_configuration_, _info);
  return new fabric_endpoint(*this, *this, *_info, remote_endpoint_, port_);
}
catch ( const fabric_runtime_error &e )
{
  throw e.add(" remote " + std::string(remote_endpoint_) + ":" + std::to_string(port_) + while_in(__func__));
}
catch ( const std::system_error &e )
{
  throw std::system_error(e.code(), e.what() + std::string(" remote ") + std::string(remote_endpoint_) + ":" + std::to_string(port_) + while_in(__func__));
}

void Fabric::bind(::fid_ep &ep_)
{
  CHECK_FI_ERR(::fi_ep_bind(&ep_, &_eq->fid, 0));
}

void Fabric::bind(::fid_pep &ep_)
{
  CHECK_FI_ERR(::fi_pep_bind(&ep_, &_eq->fid, 0));
}

void Fabric::register_pep(::fid_t ep_, event_consumer &ec_)
{
  std::lock_guard<std::mutex> g{_m_eq_dispatch_pep};
  auto p =
    _eq_dispatch_pep.insert(eq_dispatch_t::value_type(ep_, &ec_));
  (void)p;
  assert(p.second);
}

void Fabric::register_aep(::fid_t ep_, event_consumer &ec_)
{
  std::lock_guard<std::mutex> g{_m_eq_dispatch_aep};
  auto p =
    _eq_dispatch_aep.insert(eq_dispatch_t::value_type(ep_, &ec_));
  (void)p;
  assert(p.second);
}

void Fabric::deregister_endpoint(::fid_t ep_)
{
  /* Don't know whether this in a active ep or a passive ep, so try to erase both */
  {
    std::lock_guard<std::mutex> g{_m_eq_dispatch_pep};
    _eq_dispatch_pep.erase(ep_);
  }
  {
    std::lock_guard<std::mutex> g{_m_eq_dispatch_aep};
    _eq_dispatch_aep.erase(ep_);
  }
}

int Fabric::fd() const
{
  return _fd;
}

std::shared_ptr<::fid_domain> Fabric::make_fid_domain(::fi_info &info_, void *context_) const
try
{
  ::fid_domain *f(nullptr);
  CHECK_FI_ERR(::fi_domain(&*_fabric, &info_, &f, context_));
  FABRIC_TRACE_FID(f);
  return fid_ptr(f);
}
catch ( const fabric_runtime_error &e )
{
  throw e.add(tostr(info_));
}

std::shared_ptr<::fid_pep> Fabric::make_fid_pep(::fi_info &info_, void *context_) const
try
{
  ::fid_pep *f;
  CHECK_FI_ERR(::fi_passive_ep(&*_fabric, &info_, &f, context_));
  static_assert(0 == FI_SUCCESS, "FI_SUCCESS not 0, which means that we need to distinguish between these types of \"successful\" returns");
  FABRIC_TRACE_FID(f);
  return fid_ptr(f);
}
catch ( const fabric_runtime_error &e )
{
  throw e.add(tostr(info_));
}

std::shared_ptr<::fid_eq> Fabric::make_fid_eq(::fi_eq_attr &attr_, void *context_) const
{
  ::fid_eq *f;
  CHECK_FI_ERR(::fi_eq_open(&*_fabric, &attr_, &f, context_));
  FABRIC_TRACE_FID(f);
  return fid_ptr(f);
}

const char *Fabric::prov_name() const noexcept
{
  return _info && _info->fabric_attr ? _info->fabric_attr->prov_name : nullptr;
}
