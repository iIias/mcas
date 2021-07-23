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


/*
 * Authors:
 *
 */

#include "fabric_factory.h"

#include "fabric.h"

/**
 * Fabric/RDMA-based network component
 *
 */

Fabric_factory::Fabric_factory()
{
}

auto Fabric_factory::make_fabric(common::string_view json_configuration) -> component::IFabric *
{
  return new Fabric(json_configuration);
}

void *Fabric_factory::query_interface(component::uuid_t& itf_uuid) {
  return itf_uuid == IFabric_factory::iid() ? this : nullptr;
}

/**
 * Factory entry point
 *
 */
extern "C" void * factory_createInstance(component::uuid_t component_id)
{
  return component_id == Fabric_factory::component_id() ? new Fabric_factory() : nullptr;
}
