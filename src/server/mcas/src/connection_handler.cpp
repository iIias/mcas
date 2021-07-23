/*
  Copyright [2017-2020] [IBM Corporation]
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
#include "connection_handler.h"
#include "connection_state.h"
#include "security.h"
#include "mcas_config.h"

static constexpr unsigned EXTRA_BISCUITS = 0;

namespace mcas
{
Connection_handler::Connection_handler(unsigned debug_level,
                                       gsl::not_null<Factory *> factory,
                                       std::unique_ptr<Preconnection> && preconnection)
  : Connection_base(debug_level, factory, std::move(preconnection)),
    Region_manager(debug_level, transport()),
    Connection_TLS_session(debug_level, this),
    _mr_vector{},
    _tick_count(0),
    _auth_id(0),
    _pending_msgs{},
    _pending_actions(),
    _pool_manager(),
    _stats{},
    _tls_buffer()
{
}

Connection_handler::~Connection_handler()
{
  dump_stats();
#if 0
  exit(0); /* for profiler, if not using --forced-exit */
#endif
}

int Connection_handler::tick()
{
  using namespace protocol;

  auto response = TICK_RESPONSE_CONTINUE;
  _tick_count++;

#if 0
  auto now = rdtsc();

  /* output IOPS */
  if (_stats.next_stamp == 0) {
    _stats.next_stamp = now + (_freq_mhz * 1000000.0);
    _stats.last_count = _stats.response_count;
  }
  if (now >= (_stats.next_stamp)) {
    PMAJOR("(%p) IOPS: %lu /s", this, _stats.response_count - _stats.last_count);
    _stats.next_stamp = now + (_freq_mhz * 1000000.0);
    _stats.last_count = _stats.response_count;
  }
#endif

  /* */
  if (check_network_completions() == Fabric_connection_base::Completion_state::CLIENT_DISCONNECT) {
    PMAJOR("Client disconnected.");
    _state = Connection_state::CLIENT_DISCONNECTED;
    return Connection_handler::TICK_RESPONSE_CLOSE;
  }

  if (_send_value_posted_count != 0) {
    ++_stats.wait_send_value_misses;
  }

  switch (_state) {
  case Connection_state::WAIT_NEW_MSG_RECV:
    if (check_for_posted_recv_complete()) /*< check for recv completion */
      {
        auto iob = posted_recv();
        assert(iob);

        const auto msg = protocol::message_cast(iob->base());
        msg_recv_log(msg, __func__);

        //        PNOTICE("Recv message: crc32 (id=%lx, len=%u)", msg->auth_id(), msg->msg_len());

        switch (msg->type_id()) {
        case MSG_TYPE::IO_REQUEST:
          if (option_DEBUG > 2) PMAJOR("Shard: IO_REQUEST");
          _pending_msgs.push(iob);
          post_recv_buffer(allocate_recv());
          break;

        case MSG_TYPE::PUT_ADO_REQUEST:
        case MSG_TYPE::ADO_REQUEST:
          if (option_DEBUG > 2) PMAJOR("Shard: ADO_REQUEST");
          _pending_msgs.push(iob);
          post_recv_buffer(allocate_recv());
          break;

        case MSG_TYPE::CLOSE_SESSION:
          post_recv_buffer(allocate_recv());
          if (option_DEBUG > 2) PMAJOR("Shard: CLOSE_SESSION");
          free_recv_buffer();
          response = TICK_RESPONSE_CLOSE;
          break;

        case MSG_TYPE::POOL_REQUEST:
          if (option_DEBUG > 2) PMAJOR("Shard: POOL_REQUEST");
          _pending_msgs.push(iob);
          post_recv_buffer(allocate_recv());
          break;

        case MSG_TYPE::INFO_REQUEST:
          if (option_DEBUG > 2) PMAJOR("Shard: INFO_REQUEST");
          _pending_msgs.push(iob);
          post_recv_buffer(allocate_recv());
          break;

        default:
          throw Logic_exception("unhandled message (type:%x)", int(msg->type_id()));
        }

        ++_stats.recv_msg_count;

        if (option_DEBUG > 2)
          PMAJOR("Shard State: %lu %p WAIT_MSG_RECV complete", _tick_count, common::p_fmt(this));
      }
    else {
      ++_stats.wait_msg_recv_misses;
    }

    break;

  case Connection_state::INITIAL: {
    static int handshakes = 0;
    handshakes++;
    if (option_DEBUG > 2)
      PMAJOR("Shard State: %lu %p POST_HANDSHAKE (%d)", _tick_count, common::p_fmt(this), handshakes);

	/* fabric connection has allocated the first receive buffer */

    /* allocating an extra buffer or three makes the hstore benchmark run about 10% faster */
    for (auto i = EXTRA_BISCUITS; i != 0; --i) {
      post_recv_buffer(allocate_recv());
    }

    set_state(Connection_state::WAIT_HANDSHAKE);
    return TICK_RESPONSE_CONTINUE;
  }

  case Connection_state::CLIENT_DISCONNECTED: {
    break;
  }

  case Connection_state::CLOSE_CONNECTION: {
    PNOTICE("### closing connection");
    return TICK_RESPONSE_CLOSE;
  }

  case Connection_state::WAIT_TLS_HANDSHAKE: {
    auto next = process_tls_session();

    if(next == Connection_state::WAIT_NEW_MSG_RECV) {
      /* authentication ID becomes that from TLS session */
      set_auth_id(tls_auth_id());
    }
    
    set_state(next);
    break;
  }

  case Connection_state::WAIT_HANDSHAKE:
    static int resp_handshakes = 0;
    if (check_for_posted_recv_complete()) {
      resp_handshakes ++;

      if (option_DEBUG > 2)
        PLOG("Shard State: %lu %p WAIT_HANDSHAKE complete (%d)",
             _tick_count, common::p_fmt(this), resp_handshakes);

      const auto iob = posted_recv();
      assert(iob);

      const auto msg = protocol::message_cast(iob->base())->ptr_cast<Message_handshake>();

      if (msg->get_status() != S_OK)
        throw General_exception("handshake status != S_OK (%d)", msg->get_status());

      set_auth_id(msg->auth_id());
      
      /* if security_tls_auth bit is set, then we need to establish a
         GNU TLS side-channel as part of this session. this is
         triggered by sending the TLS port for the client to accept
         on.
      */
      if(msg->is_tls_auth()) {
        PNOTICE("TLS is ON (auth)");
        set_security_options(true, false /* hmac */);
        respond_to_handshake(true);
        set_state(Connection_state::WAIT_TLS_HANDSHAKE);
        return TICK_RESPONSE_WAIT_SECURITY;
      }
      else {
        respond_to_handshake(false);
        PNOTICE("TLS is OFF");
      }

      free_buffer(iob);
      
    }
    break;
  }  // end switch
  return response;
}


void Connection_handler::configure_security(const std::string& bind_ipaddr,
                                            const unsigned bind_port,
                                            const std::string& cert_file,
                                            const std::string& key_file)
{
  if(Connection_base::debug_level() > 1)
    PLOG("config security (addr:%s, port:%u)", bind_ipaddr.c_str(), bind_port);
  
  if(bind_ipaddr.empty() == false) {
    set_security_binding(bind_ipaddr, bind_port);
    set_security_params(cert_file, key_file);
  }
}
  
void Connection_handler::respond_to_handshake(bool start_tls)
{
  auto reply_iob = allocate_send();
  assert(reply_iob);

  auto reply_msg = new (reply_iob->base()) protocol::Message_handshake_reply(reply_iob->length(),
                                                                             auth_id(),
                                                                             1 /* seq */,
                                                                             max_message_size(),
                                                                             reinterpret_cast<uint64_t>(this),
                                                                             start_tls);

  /* post response */
  reply_iob->set_length(reply_msg->msg_len());
  post_recv_buffer(allocate_recv());
  post_send_buffer(reply_iob, reply_msg, __func__);
  set_state(Connection_state::WAIT_NEW_MSG_RECV);
}




}  // namespace mcas
