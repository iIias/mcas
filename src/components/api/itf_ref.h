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
#ifndef __ITF_REF_H__
#define __ITF_REF_H__

#include <memory>

namespace component
{
/* deleter */
template <typename T>
struct ref_delete
{
  constexpr ref_delete() noexcept = default;
  void operator() (T *t) const
  {
    t->release_ref();
  }
};

/* despite the name, acts like a pointer, not like a reference */
template <class I>
using Itf_ref = std::unique_ptr<I, ref_delete<I>>;

template <typename Obj>
Itf_ref<Obj> make_itf_ref(Obj *obj_)
{
  return Itf_ref<Obj>(obj_);
}

}  // namespace component
#endif
