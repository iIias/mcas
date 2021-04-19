/*
  Copyright [2021] [IBM Corporation]
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


#ifndef __MM_PASSTHRU_COMPONENT_H__
#define __MM_PASSTHRU_COMPONENT_H__

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#define PREFIX "Rca_LB_memory_manager:"

#include <string.h>
#include <string>
#include <set>
#include <memory>
#include <common/logging.h>
#include <api/mm_itf.h>


/** 
 * Pass through memory allocation to libc
 * 
 */
class Passthru_memory_manager : public component::IMemory_manager_volatile
{
public:
  Passthru_memory_manager(const unsigned debug_level)
  {
    if(debug_level > 0)
      PLOG("Passthru_memory_manager: ctor");
  }

  virtual ~Passthru_memory_manager() {
  }
  
  DECLARE_VERSION(0.1f);
  DECLARE_COMPONENT_UUID(0x89251217,0xc755,0x4558,0x8bb4,0x4a,0xb1,0x0e,0x89,0xcd,0x15);

  void* query_interface(component::uuid_t& itf_uuid) override
  {
    if (itf_uuid == component::IMemory_manager_volatile_reconstituting::iid()) {
      return static_cast<component::IMemory_manager_volatile*>(this);
    }
    else return NULL;  // we don't support this interface
  }

  void unload() override { delete this; }

public:
  virtual void debug_dump() override {
    PINF("MM: Passthru");
  }

  virtual status_t allocate(std::size_t n, void **out_ptr) override {
    assert(out_ptr);
    *out_ptr = ::malloc(n);
    PINF("MM: PASSTHRU LB - ALLOC(%lu) --> %p", n, *out_ptr);
    return S_OK;
  }

  virtual status_t aligned_allocate(size_t n, size_t alignment, void **out_ptr) override {
    assert(out_ptr);
    *out_ptr = ::aligned_alloc(alignment, n);
    PINF("MM: PASSTHRU LB - ALIGNED_ALLOC(%lu, %lu) --> %p", n, alignment, *out_ptr);
    return S_OK;
  }

  virtual status_t deallocate(void * ptr, std::size_t size = 0) override {
    PINF("MM: PASSTHRU LB - FREE(%p, %lu)", ptr, size);
    ::free(ptr);
    return S_OK;
  }
  
  virtual status_t callocate(size_t n, void ** out_ptr) override  {
    assert(out_ptr);
    PINF("MM: PASSTHRU LB - CALLOC(%lu) --> %p", n, *out_ptr);        
    *out_ptr = ::calloc(1, n);
    PINF("MM: PASSTHRU LB - CALLOC(%lu) --> %p", n, *out_ptr);        
    return S_OK;
  }

  virtual status_t reallocate(void *ptr, size_t size, void **out_ptr) override {
    assert(out_ptr);
    *out_ptr = ::realloc(ptr, size);
    return S_OK;
  }
  
private:

};


/** 
 * Factory
 * 
 */
class Passthru_memory_manager_factory : public component::IMemory_manager_factory {
public:
  DECLARE_VERSION(0.1f);

  /* index_factory - see components.h */
  DECLARE_COMPONENT_UUID(0xfac51217,0xc755,0x4558,0x8bb4,0x4a,0xb1,0x0e,0x89,0xcd,0x15);

  void* query_interface(component::uuid_t& itf_uuid) override
  {
    if (itf_uuid == component::IMemory_manager_factory::iid())
      return static_cast<component::IMemory_manager_factory*>(this);
    else return NULL;  // we don't support this interface
  }

  void unload() override { delete this; }

  virtual component::IMemory_manager_volatile *
  create_mm_volatile(const unsigned debug_level) override
  {
    auto new_instance = static_cast<component::IMemory_manager_volatile *>
      (new Passthru_memory_manager(debug_level));
    
    new_instance->add_ref();
    return new_instance;    
  }
  
  virtual component::IMemory_manager_volatile_reconstituting *
  create_mm_volatile_reconstituting(const unsigned debug_level) override
  {
    return nullptr;    
  }
  
  virtual component::IMemory_manager_persistent *
  create_mm_persistent(const unsigned /*debug_level*/) override
  {
    return nullptr;
  }
  
};


#pragma GCC diagnostic pop

#endif // __MM_RCALB_COMPONENT_H__
