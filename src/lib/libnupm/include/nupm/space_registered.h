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

#ifndef _NUPM_SPACE_REGISTERED_H_
#define _NUPM_SPACE_REGISTERED_H_

#include "space_opened.h"
#include <common/logging.h>
#include <experimental/string_view>
#include <string>
#include <vector>
#include <sys/uio.h>

namespace nupm
{
struct dax_manager;

struct path_use
  : public common::log_source
{
  using string_view = std::experimental::string_view;
private:
  /* Would include a common::moveable_ptr<dax_manager>, except that the
   * registry is static, potentially covering multiple dax_manager instances.
   */
  std::string _name;
public:
  path_use(const common::log_source &ls, const string_view &name);
  path_use(const path_use &) = delete;
  path_use &operator=(const path_use &) = delete;
  path_use(path_use &&) noexcept;
  ~path_use();
  const std::string & path_name() const { return _name; }
};

struct space_registered
{
  using string_view = std::experimental::string_view;
private:
  path_use _pu;
public:
  space_opened _or;
public:
  space_registered(
    const common::log_source &ls
    , dax_manager * dm
#if 0
    , const std::string& file
#endif
    , common::fd_locked &&fd
    , const string_view &path
    , addr_t base_addr
  );
#if 0
  space_registered(
    const common::log_source &ls
    , dax_manager * dm
    , const std::string& file
    , const string_view &name
    , const std::vector<::iovec> &mapping
  );
#endif
  space_registered(
    const common::log_source &ls
    , dax_manager * dm
    , common::fd_locked &&fd
    , const string_view &name
    , const std::vector<::iovec> &mapping
  );

  space_registered(const space_registered &) = delete;
  space_registered &operator=(const space_registered &) = delete;
  space_registered(space_registered &&) noexcept = default;
  const std::string & path_name() const { return _pu.path_name(); }
};
}  // namespace nupm

#endif
