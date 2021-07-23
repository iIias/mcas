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


#ifndef _FABRIC_CONNECTION_CLIENT_H_
#define _FABRIC_CONNECTION_CLIENT_H_

#include "fabric_connection.h"

#include "fabric_types.h"
#include <common/string_view.h>

#include <cstdint> /* uint16_t */
#include <string>

struct fi_info;

struct event_producer;
struct fabric_endpoint;

class Fabric_connection_client
  : public fabric_connection
{
  event_producer &_ev;

  /* BEGIN fabric_connection */
  /**
   * @throw fabric_bad_alloc : std::bad_alloc - libfabric out of memory (creating a new server)
   * @throw std::system_error - writing event pipe (normal callback)
   * @throw std::system_error - writing event pipe (readerr_eq)
   */
  void solicit_event() const override;
  /*
   * @throw fabric_runtime_error : std::runtime_error : ::fi_control(FI_GETWAIT) fail
   */
  void wait_event() const override;
  /* END fabric_connection */

  /*
   * @throw std::system_error : pselect fail
   * @throw std::system_error : read error on event pipe
   * @throw fabric_bad_alloc : std::bad_alloc - libfabric out of memory (creating a new server)
   * @throw std::system_error - writing event pipe (normal callback)
   * @throw std::system_error - writing event pipe (readerr_eq)
   */
  void expect_event_sync(std::uint32_t event_exp) const;

public:
  /*
   * @throw bad_dest_addr_alloc : std::bad_alloc
   * @throw fabric_runtime_error : std::runtime_error : ::fi_domain fail
   * @throw fabric_bad_alloc : std::bad_alloc - fabric allocation out of memory
   * @throw fabric_runtime_error : std::runtime_error : ::fi_connect fail
   *
   * @throw fabric_bad_alloc : std::bad_alloc - out of memory
   * @throw fabric_runtime_error : std::runtime_error : ::fi_ep_bind fail
   * @throw fabric_runtime_error : std::runtime_error : ::fi_enable fail
   * @throw fabric_runtime_error : std::runtime_error : ::fi_ep_bind fail (event registration)
   *
   * @throw std::logic_error : socket initialized with a negative value (from ::socket) in Fd_control
   * @throw std::logic_error : unexpected event
   * @throw std::system_error (receiving fabric server name)
   * @throw std::system_error : pselect fail (expecting event)
   * @throw std::system_error : resolving address
   *
   * @throw std::system_error : read error on event pipe
   * @throw std::system_error : pselect fail
   * @throw std::system_error : read error on event pipe
   * @throw fabric_bad_alloc : std::bad_alloc - libfabric out of memory (creating a new server)
   * @throw std::system_error - writing event pipe (normal callback)
   * @throw std::system_error - writing event pipe (readerr_eq)
   * @throw std::system_error - receiving data on socket
   */
  explicit Fabric_connection_client(
    component::IFabric_endpoint_unconnected_client *aep
    , event_producer &ep
	, fabric_types::addr_ep_t _peer_addr
  );
  Fabric_connection_client(const Fabric_connection_client &) = delete;
  Fabric_connection_client &operator=(const Fabric_connection_client &) = delete;
  ~Fabric_connection_client();
};

#endif
