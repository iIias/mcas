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


#include "fabric_server_generic_factory.h"

#include "event_expecter.h"
#include "event_producer.h"
#include "fabric.h"
#include "fabric_check.h"
#include "fabric_endpoint_server.h"
#include "fabric_util.h" /* get_name */
#include "fd_control.h"
#include "system_fail.h"

#include "rdma-fi_cm.h" /* fi_listen */

#include <common/pointer_cast.h>
#include <unistd.h> /* write */
#include <netinet/in.h> /* sockaddr_in */
#include <sys/select.h> /* fd_set, pselect */

#include <algorithm> /* max */
#include <cerrno>
#include <chrono> /* seconds */
#include <exception>
#include <functional> /* ref */
#include <iostream> /* cerr */
#include <memory> /* make_shared */
#include <string> /* to_string */
#include <thread> /* sleep_for */

Fabric_server_generic_factory::Fabric_server_generic_factory(Fabric &fabric_, event_producer &eq_, ::fi_info &info_, std::uint32_t addr_, std::uint16_t port_)
  : _info(info_)
  , _fabric(fabric_)
  , _pep(fabric_.make_fid_pep(_info, this))
  /* register as an event consumer */
  , _event_registration(eq_, *this, *_pep)
  , _m_pending{}
  , _pending{}
  , _open{}
  , _end{}
  , _eq{eq_}
  , _listen_exception(nullptr)
  , _listener(
    std::async(
      std::launch::async
      , &Fabric_server_generic_factory::listen
      , this
      , addr_
      , port_
      , _end.fd_read()
      , std::ref(*_pep)
    )
  )
{
}

Fabric_server_generic_factory::~Fabric_server_generic_factory()
{
  char c{};
  auto sz = ::write(_end.fd_write(), &c, 1);
  if ( sz != 1 )
  {
    std::cerr << __func__ << ": failed to signal end of connection" << "\n";
  }
  try
  {
    _listener.get();
  }
  catch ( const std::exception &e )
  {
    std::cerr << __func__ << ": SERVER connection error: " << e.what() << "\n";
  }
}

size_t Fabric_server_generic_factory::max_message_size() const noexcept
{
  return _info.ep_attr->max_msg_size;
}

std::string Fabric_server_generic_factory::get_provider_name() const
{
  return _info.fabric_attr->prov_name;
}

static int pending_count = 1;

void Fabric_server_generic_factory::cb(std::uint32_t event, ::fi_eq_cm_entry &entry_) noexcept
try
{
  switch ( event )
  {
  case FI_CONNREQ:
    {
      auto aep = std::unique_ptr<component::IFabric_endpoint_unconnected_server>(new fabric_endpoint_server(_fabric, _eq, *entry_.info));
      std::lock_guard<std::mutex> g{_m_pending};
      pending_count++;
      _pending.push(std::move(aep));
    }
    break;
  default:
    break;
  }
}
catch ( const std::exception &e )
{
  std::cerr << __func__ << " (Fabric_server_factory) " << e.what() << "\n";
}

void Fabric_server_generic_factory::err(::fid_eq *, ::fi_eq_err_entry &) noexcept
{
  /* The passive endpoint receives an error. As it is not a connection request, ignore it. */
}

/*
 * @throw std::system_error - ::setsockopt
 * @throw std::system_error - ::bind
 * @throw std::system_error - ::listem
 */
Fd_socket Fabric_server_generic_factory::make_listener(std::uint32_t ip_addr, std::uint16_t port)
{
  constexpr int domain = AF_INET;
  Fd_socket fd(::socket(domain, SOCK_STREAM, 0));

  {
    /* Note: setsockopt usually does more good than harm, but the decision to
     * use it should probably be left to the user.
     */
    int optval = 1;
    if ( -1 == ::setsockopt(fd.fd(), SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval) )
    {
      auto e = errno;
      system_fail(e, "setsockopt");
    }
  }

  {
    sockaddr_in addr{};
    addr.sin_family = domain;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(ip_addr);

    if ( -1 == ::bind(fd.fd(), common::pointer_cast<sockaddr>(&addr), sizeof addr) )
    {
      auto e = errno;
      system_fail(e, "bind for port " + std::to_string(port));
    }
  }

  if ( -1 == ::listen(fd.fd(), 10) )
  {
    auto e = errno;
    system_fail(e, "Server_control::make_listener ::listen ");
  }

  return fd;
}

/*
 * objects accessed in multiple threads:
 *
 *  fabric_ should be used only by (threadsafe) libfabric calls.
 *  info_ should be used only in by (threadsafe) libfabric calls.
 * _pending: has a mutex
 */

void Fabric_server_generic_factory::listen(
  std::uint32_t ip_addr_
  , std::uint16_t port_
  , int end_fd_
  , ::fid_pep &pep_
)
try
{
  Fd_socket listen_fd(make_listener(ip_addr_, port_));

  /* The endpoint now has a name, which we can advertise. */
  CHECK_FI_ERR(::fi_listen(&pep_));

  fabric_types::addr_ep_t pep_name(get_name(&_pep->fid));

  listen_loop(end_fd_, pep_name, listen_fd);
}
catch ( const std::exception &e )
{
  std::cerr << __func__ << ": listen failure " << e.what() << "\n";
  _listen_exception = std::current_exception();
  throw;
}

void Fabric_server_generic_factory::listen_loop(
  int end_fd_
  , const fabric_types::addr_ep_t &pep_name_
  , const Fd_socket &listen_fd_
) noexcept
{
  auto run = true;
  while ( run )
  {
    fd_set fds_read;
    FD_ZERO(&fds_read);
    FD_SET(listen_fd_.fd(), &fds_read);
    FD_SET(_eq.fd(), &fds_read);
    FD_SET(end_fd_, &fds_read);

    auto n = ::pselect(std::max(std::max(_eq.fd(), listen_fd_.fd()), end_fd_)+1, &fds_read, nullptr, nullptr, nullptr, nullptr);
    if ( n < 0 )
    {
      auto e = errno;
      switch ( e )
      {
      /* Cannot "fix" any of the error conditions, but do acknowledge their possibility */
      case EBADF:
      case EINTR:
      case EINVAL:
      case ENOMEM:
        break;
      default: /* unknown error */
        break;
      }
    }
    else
    {
      run = ! FD_ISSET(end_fd_, &fds_read);
      if ( FD_ISSET(listen_fd_.fd(), &fds_read) )
      {
        try
        {
          auto r = ::accept(listen_fd_.fd(), nullptr, nullptr);
          if ( r == -1 )
          {
            auto e = errno;
            system_fail(e, (" in accept fd " + std::to_string(listen_fd_.fd())));
          }

          Fd_control conn_fd(r);
          /* NOTE: Fd_control needs a timeout. */

          /* We have a "control connection". Send the name of the server passive endpoint to the client */
          conn_fd.send_name(pep_name_);
        }
        catch ( const std::exception &e )
        {
          std::cerr << "exception establishing connection: " << e.what() << "\n";
          std::this_thread::sleep_for(std::chrono::seconds(1));
#if 0
          throw;
          /* An exception may not cause the loop to exit; only the destructor may do that. */
#endif
        }
      }
      if ( FD_ISSET(_eq.fd(), &fds_read) )
      {
        try
        {
          /* There may be something in the event queue. Go see what it is. */
          _eq.read_eq();
        }
        catch ( const std::bad_alloc &e )
        {
          std::cerr << "bad_alloc in event queue: " << e.what() << "\n";
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        catch ( const std::exception &e )
        {
          std::cerr << "exception handling event queue: " << e.what() << "\n";
          std::this_thread::sleep_for(std::chrono::seconds(1));
#if 0
          throw;
          /* An exception may not cause the loop to exit; only the destructor may do that. */
#endif
        }
      }
    }
  }
}

auto Fabric_server_generic_factory::get_new_endpoint_unconnected() -> component::IFabric_endpoint_unconnected_server *
{
  if ( _listen_exception )
  {
    std::cerr << __func__ << ": _listen_exception present, rethrowing\n";
    std::rethrow_exception(_listen_exception);
  }

  std::lock_guard<std::mutex> g{_m_pending};
  return _pending.remove().release();
}

void Fabric_server_generic_factory::open_connection_generic(event_expecter *c)
{
	c->expect_event(FI_CONNECTED);
	_open.add(c);
}

std::vector<event_expecter *> Fabric_server_generic_factory::connections()
{
  return _open.enumerate();
}

void Fabric_server_generic_factory::close_connection(event_expecter * cnxn_)
{
  _open.remove(cnxn_);
}
