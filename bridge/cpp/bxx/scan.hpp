/*
This file is part of Bohrium and copyright (c) 2012 the Bohrium team:
http://bohrium.bitbucket.org

Bohrium is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as 
published by the Free Software Foundation, either version 3 
of the License, or (at your option) any later version.

Bohrium is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the 
GNU Lesser General Public License along with Bohrium. 

If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef __BOHRIUM_BRIDGE_CPP_SCAN
#define __BOHRIUM_BRIDGE_CPP_SCAN

namespace bxx {

template <typename T>
multi_array<T>& scan(multi_array<T>& op, scannable opcode, size_t axis)
{
    multi_array<T>* result = &Runtime::instance().temp<T>(op);
    result->link();                         // Bind the base

    switch(opcode) {
        case SUM:
            bh_add_accumulate(*result, op, (bh_int64)axis);
            break;
        case PRODUCT:
            bh_multiply_accumulate(*result, op, (bh_int64)axis);
            break;
        default:
            throw std::runtime_error("Error: Unsupported opcode for scan.\n");
    }

    return *result;
}

}
#endif

