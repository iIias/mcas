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


#include "fabric_connection.h"

#include "fabric_endpoint.h"

fabric_connection::fabric_connection(
	component::IFabric_endpoint_unconnected_client *aep_
		, fabric_types::addr_ep_t peer_addr_
)
	: _aep(static_cast<fabric_endpoint *>(aep_))
	, _peer_addr(peer_addr_)
{
}

fabric_connection::fabric_connection(
	component::IFabric_endpoint_unconnected_server *aep_
		, fabric_types::addr_ep_t peer_addr_
)
	: _aep(static_cast<fabric_endpoint *>(aep_))
	, _peer_addr(peer_addr_)
{
}

fabric_endpoint *fabric_connection::aep() const
{
	return _aep;
}

std::string fabric_connection::get_peer_addr()
{
	return std::string(_peer_addr.begin(), _peer_addr.end());
}

std::string fabric_connection::get_local_addr()
{
	auto v = get_name();
	return std::string(v.begin(), v.end());
}

auto fabric_connection::get_name() const -> fabric_types::addr_ep_t
{
	auto it = static_cast<const char *>(aep()->ep_info().src_addr);
	return fabric_types::addr_ep_t(
		std::vector<char>(it, it+aep()->ep_info().src_addrlen)
	);
}

std::size_t fabric_connection::max_message_size() const noexcept
{
  return aep()->ep_info().ep_attr->max_msg_size;
}

std::size_t fabric_connection::max_inject_size() const noexcept
{
  return aep()->ep_info().tx_attr->inject_size;
}
