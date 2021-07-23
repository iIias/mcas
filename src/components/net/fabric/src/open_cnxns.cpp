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


/*
 * Authors:
 *
 */

#include "open_cnxns.h"

#include "event_expecter.h"

#include <algorithm> /* lower_bound, transform */
#include <iterator> /* back_inserter */

using guard = std::unique_lock<std::mutex>;

Open_cnxns::Open_cnxns()
  : _m{}
  , _s{}
{}

void Open_cnxns::add(cnxn_type *c_)
{
  guard g{_m};
  _s.insert(owned_type(c_));
}

void Open_cnxns::remove(cnxn_type *c_)
{
  guard g{_m};
  auto it =
    std::lower_bound(
      _s.begin()
      , _s.end()
      , c_
      , [] (const owned_type &e, cnxn_type *c) { return e.get() < c; }
    );
  if ( it != _s.end() && it->get() == c_ )
  {
    _s.erase(it);
  }
}

auto Open_cnxns::enumerate() -> std::vector<cnxn_type *>
{
  guard g{_m};
  std::vector<cnxn_type *> v;

  std::transform(
    _s.begin()
    , _s.end()
    , std::back_inserter(v)
    , [] (const owned_type &v_)
      {
        return &*v_;
      }
  );

  return v;
}
