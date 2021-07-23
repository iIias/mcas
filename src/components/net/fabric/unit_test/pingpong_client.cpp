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
#include "pingpong_client.h"

#include "patience.h" /* open_connection_patiently */
#include "pingpong_cb_pack.h"
#include "pingpong_cnxn_state.h"

#include <common/errors.h> /* S_OK */
#include <common/types.h> /* status_t */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <gtest/gtest.h>
#pragma GCC diagnostic pop

#include <exception>
#include <iostream> /* cerr */

namespace component
{
  class IFabric;
}

namespace
{
  void send_cb(cb_ctxt *tx_ctxt_, ::status_t stat_, std::size_t)
  {
    auto &bt = tx_ctxt_->buffer();
    EXPECT_EQ(stat_, S_OK);
    EXPECT_EQ(bt.v.size(), 1U);
    ++static_cast<uint8_t *>(bt.v.front().iov_base)[1];
  }
  void recv_cb(cb_ctxt *rx_ctxt_, ::status_t stat_, std::size_t len_)
  {
    auto &cs = rx_ctxt_->state();
    auto &br = rx_ctxt_->buffer();
    EXPECT_EQ(stat_, S_OK);
    EXPECT_EQ(len_, cs.msg_size);
    EXPECT_EQ(br.v.size(), 1U);
    if ( cs.iterations_left == 0 )
    {
      cs.done = true;
    }
    if ( ! cs.done )
    {
      auto tx_ctxt = rx_ctxt_->response();
      auto &bt = tx_ctxt->buffer();
      cs.comm().post_recv(&*br.v.begin(), &*br.v.end(), &*br.d.begin(), rx_ctxt_);
      cs.send(bt, tx_ctxt);
      ++static_cast<uint8_t *>(bt.v.front().iov_base)[1];
    }
  }
}

pingpong_client::pingpong_client(
  component::IFabric &fabric_
  , const std::string &fabric_spec_
  , const std::string ip_address_
  , std::uint16_t port_
  , std::uint64_t buffer_size_
  , std::uint64_t remote_key_base_
  , unsigned iteration_count_
  , std::uint64_t msg_size_
  , std::uint8_t id_
)
try
  : _ep(fabric_.make_endpoint(fabric_spec_, ip_address_, port_))
  , _cnxn(open_connection_patiently(_ep.get()))
  , _stat{}
  , _id{id_}
{
  cnxn_state c(
    *_cnxn
    , iteration_count_
    , msg_size_
  );

  /* executes a poll_recv per receive buffer, establishing the callbacks */
  cb_pack pack{
    c
    , *_cnxn
    , send_cb
    , recv_cb
    , buffer_size_
    , remote_key_base_
    , msg_size_
  };

  std::uint64_t poll_count = 0U;

  _stat.do_start();

  auto &tx_ctxt = pack._tx_ctxt;
  auto &buffer = tx_ctxt.buffer();
  buffer._rm[0] = static_cast<char>(_id);
  buffer._rm[1] = 0;
  c.send(buffer, &tx_ctxt);
  while ( ! c.done )
  {
    ++poll_count;
    c.comm().poll_completions(cb_ctxt::cb);
  }

  _stat.do_stop(poll_count);
}
catch ( std::exception &e )
{
  std::cerr << __func__ << ": " << e.what() << "\n";
  throw;
}

pingpong_stat pingpong_client::time() const
{
  return _stat;
}

pingpong_client::~pingpong_client()
{
}
