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


/*
 * Authors:
 *
 */

#include "pending_cnxns.h"
#include <api/fabric_itf.h>

Pending_cnxns::Pending_cnxns()
  : _m{}
  , _q{}
{}

void Pending_cnxns::push(cnxn_t && c)
{
  guard g{_m};
  _q.push(std::move(c));
}

auto Pending_cnxns::remove() -> cnxn_t
{
  cnxn_t c;
  guard g{_m};
  if ( _q.size() != 0 )
  {
    c = std::move(_q.front());
    _q.pop();
  }
  return c;
}
