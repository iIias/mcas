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


#include "fd_pair.h"

#include "system_fail.h"

#include <unistd.h> /* close, pipe */
#include <fcntl.h> /* fcntl, O_NONBLOCK */

#include <cerrno>

Fd_pair::Fd_pair()
  : _read{}
  , _write{}
{
  int pair[2];
  if ( -1 == ::pipe(pair) )
  {
    auto e = errno;
    system_fail(e, "creating Fd_pair");
  }
  _read = common::Fd_open(pair[0]);
  _write = common::Fd_open(pair[1]);
}

std::size_t Fd_pair::read(void *b, std::size_t sz) const
{
  auto ct = ::read(fd_read(), b, sz);
  if ( ct < 0 )
  {
    system_fail(errno, __func__);
  }
  return std::size_t(ct);
}

std::size_t Fd_pair::write(const void *b, std::size_t sz) const
{
  auto ct = ::write(fd_write(), const_cast<void *>(b), sz);
  if ( ct < 0 )
  {
    system_fail(errno, std::string(__func__) + " fd=" + std::to_string(fd_write()));
  }
  return std::size_t(ct);
}
