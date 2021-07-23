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


/*
 * Authors:
 *
 */

#include "fd_control.h"

#include "addrinfo.h"
#include "system_fail.h"

#include <netdb.h> /* addrinfo? */
#include <sys/socket.h>

#include <memory> /* shared_ptr */
#include <string> /* to_string */

Fd_control::Fd_control()
 : Fd_socket()
{}

Fd_control::Fd_control(int fd_)
  : Fd_socket(fd_)
{
  /* NOTE: consider adding setsockopt(SO_RCVTIMEO) here */
}

namespace
{
  Fd_socket socket_from_address(common::string_view dst_addr, uint16_t port)
  {
    auto results = getaddrinfo_ptr(dst_addr, port);
    auto e = ENOENT;

    Fd_socket fd{};
    bool ok = false;
    std::string e_why = ": " + std::string(dst_addr) + ":" + std::to_string(port);
    for ( auto rp = results.get(); rp && ! ok; rp = rp->ai_next)
    {
      fd = Fd_socket(::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
      e = errno;
      if ( fd.good() )
      {
        if ( -1 == ::connect(fd.fd(), rp->ai_addr, rp->ai_addrlen) )
        {
          e = errno;
          e_why += ", " + std::to_string(rp->ai_family) + "/" + std::to_string( rp->ai_socktype) + std::to_string( rp->ai_protocol) + "/" + std::to_string(rp->ai_addrlen) + " err " + std::to_string(e);
        }
        else
        {
          ok = true;
        }
      }
    }

    if ( ! ok )
    {
      system_fail(e, __func__ + e_why);
    }

    return fd;
  }
}

Fd_control::Fd_control(common::string_view dst_addr, std::uint16_t port)
  : Fd_socket(socket_from_address(dst_addr, port))
{
}

void Fd_control::send_name(const fabric_types::addr_ep_t &name_) const
{
  auto sz = name_.size();
  auto nsz = htonl(std::uint32_t(sz));
  send(&nsz, sizeof nsz);
  send(&*name_.begin(), sz);
}

namespace
{
  std::string while_in(common::string_view where)
  {
    return " (while in " + std::string(where) + ")";
  }
}

auto Fd_control::recv_name() const -> fabric_types::addr_ep_t
try
{
  auto nsz = htonl(0);
  recv(&nsz, sizeof nsz);

  auto sz = ntohl(nsz);
  std::vector<char> name(sz);
  recv(&*name.begin(), sz);

  return fabric_types::addr_ep_t(std::move(name));
}
catch ( const std::system_error &e )
{
  throw std::system_error(e.code(), e.what() + while_in(__func__));
}
