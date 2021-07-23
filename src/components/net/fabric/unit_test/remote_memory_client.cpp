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
#include "remote_memory_client.h"

#include "eyecatcher.h" /* remote_memory_offset */
#include "patience.h" /* open_connection_patiently */
#include "registered_memory.h"
#include "wait_poll.h"

#include <api/fabric_itf.h> /* _Fabric_client */
#include <common/errors.h> /* S_OK */
#include <common/types.h> /* status_t */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <gtest/gtest.h>
#pragma GCC diagnostic pop

#include <sys/uio.h> /* iovec */
#include <boost/io/ios_state.hpp>
#include <algorithm> /* copy */
#include <cstring> /* memcpy */
#include <exception>
#include <iomanip> /* hex */
#include <iostream> /* cerr */
#include <string>
#include <memory> /* make_shared */
#include <vector>

void remote_memory_client::check_complete_static(void *t_, void *ctxt_, ::status_t stat_, std::size_t len_)
try
{
  /* The callback context must be the object which was polling. */
  ASSERT_EQ(t_, ctxt_);
  auto rmc = static_cast<remote_memory_client *>(ctxt_);
  ASSERT_TRUE(rmc);
  rmc->check_complete(stat_, len_);
}
catch ( std::exception &e )
{
  std::cerr << "remote_memory_client::" << __func__ << e.what() << "\n";
}

void remote_memory_client::check_complete(::status_t stat_, std::size_t)
{
  _last_stat = stat_;
}

void remote_memory_client::do_quit()
{
  _quit_flag = 'q';
}

remote_memory_client::remote_memory_client(
  component::IFabric &fabric_
  , const std::string &fabric_spec_
  , const std::string ip_address_
  , std::uint16_t port_
  , std::size_t memory_size_
  , std::uint64_t remote_key_base_
)
try
  : _ep(fabric_.make_endpoint(fabric_spec_, ip_address_, port_))
  , _cnxn(open_connection_patiently(_ep.get()))
  , _rm_out{std::make_shared<registered_memory>(*_cnxn, memory_size_, remote_key_base_ * 2U)}
  , _rm_in{std::make_shared<registered_memory>(*_cnxn, memory_size_, remote_key_base_ * 2U + 1)}
  , _vaddr{}
  , _key{}
  , _quit_flag('n')
  , _last_stat(::E_FAIL)
{
  std::vector<::iovec> v;
  ::iovec iv;
  iv.iov_base = &rm_out()[0];
  iv.iov_len = (sizeof _vaddr) + (sizeof _key);
  v.emplace_back(iv);
  _cnxn->post_recv(v, this);
  ::wait_poll(
      *_cnxn
    , [this] (void *ctxt_, ::status_t stat_, std::uint64_t, std::size_t len_, void *) -> void
      {
        ASSERT_EQ(ctxt_, this);
        ASSERT_EQ(stat_, ::S_OK);
        ASSERT_EQ(len_, (sizeof _vaddr) + sizeof( _key));
        std::memcpy(&_vaddr, &rm_out()[0], sizeof _vaddr);
        std::memcpy(&_key, &rm_out()[sizeof _vaddr], sizeof _key);
      }
  );
  boost::io::ios_flags_saver sv(std::cerr);
  std::cerr << "Client: remote memory addr " << reinterpret_cast<void*>(_vaddr) << " key " << std::hex << _key << std::endl;
}
catch ( std::exception &e )
{
  std::cerr << __func__ << ": " << e.what() << "\n";
  throw;
}

void remote_memory_client::send_disconnect(component::IFabric_endpoint_comm &cnxn_, registered_memory &rm_, char quit_flag_)
{
  send_msg(cnxn_, rm_, &quit_flag_, sizeof quit_flag_);
}

remote_memory_client::~remote_memory_client()
{
  if ( _cnxn )
  {
    try
    {
      send_disconnect(cnxn(), rm_out(), _quit_flag);
    }
    catch ( std::exception &e )
    {
      std::cerr << __func__ << " exception " << e.what() << eyecatcher << std::endl;
    }
  }
}

void remote_memory_client::write(const std::string &msg_, bool force_error_)
{
  std::copy(msg_.begin(), msg_.end(), &rm_out()[0]);
  std::vector<::iovec> buffers(1);
  {
    buffers[0].iov_base = &rm_out()[0];
    std::size_t adjust = 0;
    if ( force_error_ )
    {
      adjust = 1U << 31U;
    }
    buffers[0].iov_len = msg_.size();
    _cnxn->post_write(buffers, _vaddr + remote_memory_offset - static_cast<unsigned long>(adjust), _key, this);
  }
  ::wait_poll(
    *_cnxn
    , [this] (void *ctxt_, ::status_t stat_, std::uint64_t, std::size_t len_, void *)
      {
        check_complete_static(this, ctxt_, stat_, len_);
      }
  );
  if ( force_error_ )
  {
    EXPECT_NE(_last_stat, ::S_OK);
  }
  else
  {
    EXPECT_EQ(_last_stat, ::S_OK);
  }
  if ( _last_stat != ::S_OK )
  {
    std::cerr << "remote_memory_client::" << __func__ << ": " << _last_stat << "\n";
  }
}

void remote_memory_client::read_verify(const std::string &msg_)
{
  std::vector<::iovec> buffers(1);
  {
    buffers[0].iov_base = &rm_in()[0];
    buffers[0].iov_len = msg_.size();
    _cnxn->post_read(buffers, _vaddr + remote_memory_offset, _key, this);
  }
  ::wait_poll(
    *_cnxn
    , [this] (void *ctxt_, ::status_t stat_, std::uint64_t, std::size_t len_, void *)
      {
        check_complete_static(this, ctxt_, stat_, len_);
      }
  );
  EXPECT_EQ(_last_stat, ::S_OK);
  if ( _last_stat != ::S_OK )
  {
    std::cerr << "remote_memory_client::" << __func__ << ": " << _last_stat << "\n";
  }
  std::string remote_msg(&rm_in()[0], &rm_in()[0] + msg_.size());
  EXPECT_EQ(msg_, remote_msg);
}

std::size_t remote_memory_client::max_message_size() const
{
  return _cnxn->max_message_size();
}
