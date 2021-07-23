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
#include "remote_memory_client_grouped.h"

#include "eyecatcher.h"
#include "patience.h"
#include "remote_memory_subclient.h"
#include "wait_poll.h"
#include <common/errors.h> /* S_OK */
#include <boost/io/ios_state.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <gtest/gtest.h>
#pragma GCC diagnostic pop

#include <sys/uio.h> /* iovec */
#include <vector>

remote_memory_client_grouped::remote_memory_client_grouped(
  component::IFabric &fabric_
  , const std::string &fabric_spec_
  , const std::string ip_address_
  , std::uint16_t port_
  , std::size_t memory_size_
  , std::uint64_t remote_key_base_
)
try
  : _ep(fabric_.make_endpoint(fabric_spec_, ip_address_, port_))
  , _cnxn(open_connection_grouped_patiently(_ep.get()))
  , _memory_size(memory_size_)
  , _rm_out{std::make_shared<registered_memory>(*_cnxn, memory_size_, remote_key_base_ * 2U)}
  , _vaddr{}
  , _key{}
  , _quit_flag('n')
  , _remote_key_index_for_startup_and_shutdown{remote_key_base_ * 2U + 1}
{
  std::vector<iovec> v;
  iovec iv;
  iv.iov_base = &rm_out()[0];
  iv.iov_len = (sizeof _vaddr) + (sizeof _key);
  v.emplace_back(iv);

  remote_memory_subclient rms(*this, memory_size_, _remote_key_index_for_startup_and_shutdown);
  auto &cnxn = rms.cnxn();

  cnxn.post_recv(v, this);
  ::wait_poll(
    cnxn
    , [this] (void *ctxt_, ::status_t stat_, std::uint64_t, std::size_t len_, void *) -> void
      {
        ASSERT_EQ(ctxt_, this);
        ASSERT_EQ(stat_, S_OK);
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

remote_memory_client_grouped::~remote_memory_client_grouped()
{
  try
  {
    remote_memory_subclient rms(*this, _memory_size, _remote_key_index_for_startup_and_shutdown);
    auto &cnxn = rms.cnxn();
    send_disconnect(cnxn, rm_out(), _quit_flag);
  }
  catch ( std::exception &e )
  {
    std::cerr << __func__ << " exception " << e.what() << eyecatcher << std::endl;
  }
}

void remote_memory_client_grouped::send_disconnect(component::IFabric_endpoint_connected &cnxn_, registered_memory &rm_, char quit_flag_)
{
  send_msg(cnxn_, rm_, &quit_flag_, sizeof quit_flag_);
}

std::unique_ptr<component::IFabric_endpoint_connected> remote_memory_client_grouped::allocate_group() const
{
  return std::unique_ptr<component::IFabric_endpoint_connected>(_cnxn->allocate_group());
}

std::size_t remote_memory_client_grouped::max_message_size() const
{
  return _cnxn->max_message_size();
}
