/*
   Copyright [2019-2021] [IBM Corporation]
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


#ifndef MCAS_MONITOR_PIN_H
#define MCAS_MONITOR_PIN_H

#include "logging.h"
#include "perishable_expiry.h"
#include "pin_control.h"
#include "test_flags.h"

template <typename Pool>
	struct monitor_pin
	{
	private:
		Pool _p;
		pin_control<typename Pool::shared_type> _c;
		char *_old_cptr; /* nullptr if HEAP_CONSISTENT */
		bool heap_consistent() const { return _p->is_crash_consistent(); }
		monitor_pin(const monitor_pin &) = delete;
		monitor_pin &operator=(const monitor_pin &) = delete;
	public:
		template <typename Value>
			monitor_pin(Value &v_, const Pool &p_, const pin_control<typename Pool::shared_type> &c_)
				: _p(p_)
				, _c(c_)
				, _old_cptr(heap_consistent() ? nullptr : v_.get_cptr().P)
			{
				((*_p).*(_c._arm))(v_.get_cptr());
			}

		char *get_cptr() const
		{
			return
				_old_cptr
				? _old_cptr
				: ((*_p).*_c._get_cptr)() // pin_data_get_cptr()
				;
		}

		~monitor_pin() noexcept(! TEST_HSTORE_PERISHABLE)
		{
			if ( ! perishable_expiry::is_current() )
			{
				((*_p).*_c._disarm)();
			}
		}
	};
#endif
