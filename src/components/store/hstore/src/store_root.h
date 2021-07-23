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


#ifndef MCAS_HSTORE_ROOT_H
#define MCAS_HSTORE_ROOT_H

#include <libpmemobj.h> /* PMEMoid */

struct store_root_t
{
  /* A pointer so that null value can indicate no allocation.
   * Locates a pc_al_t.
   * - all allocated space can be accessed through pc
   */
  PMEMoid persist_oid;
  /*
   * If using allocator_co, locates a heap_co, which can be used to construct a allocator_co
   */
  PMEMoid heap_oid;
};

#endif
