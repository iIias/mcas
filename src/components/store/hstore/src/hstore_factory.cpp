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

#include "hstore.h"

#include "dax_manager.h"

#include <common/json.h>
#include <common/string_view.h>
#include <common/utils.h>

#include <algorithm> /* max */
#include <cstdlib> /* getenv */
#include <string>

using IKVStore = component::IKVStore;

/**
 * Factory entry point
 *
 */
extern "C" void * factory_createInstance(component::uuid_t component_id)
{
  return
    component_id == hstore_factory::component_id()
    ? new ::hstore_factory()
    : nullptr
    ;
}

void * hstore_factory::query_interface(component::uuid_t& itf_uuid)
{
  return itf_uuid == component::IKVStore_factory::iid()
     ? static_cast<component::IKVStore_factory *>(this)
     : nullptr
     ;
}

void hstore_factory::unload()
{
  delete this;
}

/*
 * See dax_manager.cpp for the schema for the JSON "dax_map" parameter.
 */
auto hstore_factory::create(
  unsigned debug_level_
  , const IKVStore_factory::map_create & mc
) -> component::IKVStore *
{
  auto debug_it = mc.find(+k_debug);
#if HEAP_MM
  auto plugin_path_it = mc.find(+k_mm_plugin_path);
  if(plugin_path_it != mc.end()) {
    PLOG("hstore parameters: plugin(%s)", plugin_path_it->second.c_str());
  }
#endif
  auto owner_it = mc.find(+k_owner);
  auto name_it = mc.find(+k_name);
  auto dax_config_it = mc.find(+k_dax_config);

  namespace c_json = common::json;
  using json = c_json::serializer<c_json::dummy_writer>;
  using common::string_view;
  unsigned map_debug_level = unsigned(debug_it == mc.end() ? 0 : std::stoul(debug_it->second));
  unsigned effective_debug_level = std::max(debug_level_, map_debug_level);
  component::IKVStore *obj =
    new hstore(
      effective_debug_level
#if HEAP_MM
      , plugin_path_it == mc.end() ? string_view{} : string_view(plugin_path_it->second)
#endif
      , owner_it == mc.end() ? string_view{} : string_view(owner_it->second)
      , name_it == mc.end() ? string_view{} : string_view(name_it->second)
      , std::make_unique<dax_manager>(
          common::log_source(effective_debug_level)
          , dax_config_it == mc.end() ? json::array().str() : dax_config_it->second
          , bool(std::getenv("DAX_RESET"))
        )
    );
  obj->add_ref();

  return obj;
}
