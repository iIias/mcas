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


#include "fabric_server_factory.h"

#include "fabric_server.h"

#include <algorithm> /* transform */
#include <iterator> /* back_inserter */
#include <memory> /* make_shared, static_pointer_cast */

Fabric_server_factory::Fabric_server_factory(Fabric &fabric_, event_producer &eq_, ::fi_info &info_, std::uint32_t addr_, std::uint16_t port_)
  : Fabric_server_generic_factory(fabric_, eq_, info_, addr_, port_)
{}

Fabric_server_factory::~Fabric_server_factory()
{}

auto Fabric_server_factory::open_connection(component::IFabric_endpoint_unconnected_server *aep) -> Fabric_server *
{
	auto conn = new Fabric_server(aep);
	/* wait for an acnowledgment from the client, and add to open list */
	open_connection_generic(conn);
	return conn;
}

std::vector<component::IFabric_server *> Fabric_server_factory::connections()
{
  auto g = Fabric_server_generic_factory::connections();
  std::vector<component::IFabric_server *> v;
  std::transform(
    g.begin()
    , g.end()
    , std::back_inserter(v)
    , [] (event_expecter *v_)
      {
        return static_cast<Fabric_server *>(&*v_);
      }
  );
  return v;
}

void Fabric_server_factory::close_connection(component::IFabric_server * cnxn_)
{
  return Fabric_server_generic_factory::close_connection(static_cast<Fabric_server *>(cnxn_));
}
