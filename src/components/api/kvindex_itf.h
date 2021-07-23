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

#ifndef __API_KVINDEX_ITF__
#define __API_KVINDEX_ITF__

#include <api/components.h>
#include <assert.h>
#include <common/exceptions.h>

#include <string>
#include <cstdlib>

namespace component
{

class IKVStore;
/**
 * Key-value index.  Index is ordered.
 */
class IKVIndex : public component::IBase {
 public:
  // clang-format off
  DECLARE_INTERFACE_UUID(0xadb5c747,0x0f5b,0x44a6,0x982b,0x36,0x54,0x1a,0x62,0x64,0xfc);
  // clang-format on

  typedef enum {
    FIND_TYPE_NONE   = 0x0,
    FIND_TYPE_NEXT   = 0x1, /*< just get the next key in order */
    FIND_TYPE_EXACT  = 0x2, /*< perform exact match comparison on key */
    FIND_TYPE_REGEX  = 0x3, /*< apply as regular expression */
    FIND_TYPE_PREFIX = 0x4, /*< match prefix only */
  } find_t;

  inline static find_t convert_find_type(int i)
  {
    static const find_t array[] = {FIND_TYPE_NONE, FIND_TYPE_NEXT, FIND_TYPE_EXACT, FIND_TYPE_REGEX, FIND_TYPE_PREFIX};
    assert(i > 0);
    if (i > 4) throw API_exception("out of enum bounds");
    return array[i];
  }

  using offset_t = uint64_t;


  /** 
   * Returning true indicates that the shard should iterate the key space
   * to populate the index.
   * 
   * 
   * @return true to iterate/rebuild key space
   */
  virtual bool iterate_key_space_on_load()
  {
    return true;
  }
  
  /**
   * Insert a key into the index
   *
   * @param key Key
   */
  virtual void insert(const std::string& key) = 0;

  /**
   * Remove a key from the index
   *
   * @param key Key
   */
  virtual void erase(const std::string& key) = 0;

  /**
   * Clear index
   *
   */
  virtual void clear() = 0;

  /**
   * Get next item.  Throw std::out_of_range for out of bounds
   *
   * @param position Position counting from zero
   *
   * @return Key
   */
  virtual std::string get(offset_t position) const = 0;

  /**
   * Return number of items in the index
   *
   *
   * @return Index count
   */
  virtual size_t count() const = 0;

  /**
   * Perform a key search
   *
   * @param key_expression Key expression to match on
   * @param begin_position Position from which to start from. Counting from 0.
   * @param find_type FIND_TYPE_NEXT, ..EXACT, ..REGEX, ..PREFIX
   * @param out_matched_position [out] Position of the match
   * @param out_matched_key Matching key result
   * @param max_comparisons Maximum number of comparisons
   *
   * @return Matched key
   */
  virtual status_t find(const std::string& key_expression,
                        offset_t           begin_position,
                        find_t             find_type,
                        offset_t&          out_matched_position,
                        std::string&       out_matched_key,
                        unsigned           max_comparisons = 0) = 0;
};

class IKVIndex_factory : public component::IBase {
 public:
  // clang-format off
  DECLARE_INTERFACE_UUID(0xfac5c747,0x0f5b,0x44a6,0x982b,0x36,0x54,0x1a,0x62,0x64,0xfc);
  // clang-format on

  /** 
   * create_dynamic is called in response to a configure AddIndex:: call
   * 
   */
  virtual IKVIndex* create_dynamic(const std::string& dax_config) = 0;


  /** 
   * create_configured is used when the secondary index is defined in configuration file 
   * 
   * @param primary_index 
   * 
   * @return 
   */
  virtual IKVIndex* create_configured(IKVStore * primary_index)
  {
    (void)primary_index;
    throw API_exception("factory::create_configure not implemented");
  }

};

}  // namespace component

#endif
