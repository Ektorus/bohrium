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
#ifndef __BOHRIUM_BRIDGE_CPP_VISUALS
#define __BOHRIUM_BRIDGE_CPP_VISUALS
#include <stdexcept>

namespace bxx {

/**
    Do a surface plot of given array.

    @param mode 0 = 2D, 1 = 3D.
    @param colormap ?
    @param lowerbound
    @param upperbound
*/
template <typename T>
inline void plot_surface(multi_array<T>& ary, unsigned int mode, int colormap, float lowerbound, float upperbound)
{
    unsigned int rank = ary.getRank();
    if ((rank < 2) || (rank > 3)) {
        throw std::out_of_range("Surface-plotting is only supported for rank 2 and 3.");
    }
    if (mode > 1) {
        throw std::out_of_range("Unsupported mode, use 0 or 1.");
    }
    // Convert the array if not float32.

    bool flat, cube;
    if (0 == mode) {    // 2D
        flat = true;
        cube = false;
    } else {            // 3D
        if (rank == 2) {
            flat = false;
            cube = false;
        } else {
            flat = false;
            cube = true;
        }
    }
    // Construct argument array for extension
    multi_array<float> args;
    args = ones<float>(5);

    args[0] = (float)colormap;
    args[1] = (float)flat;
    args[2] = (float)cube;
    args[3] = (float)lowerbound;
    args[4] = (float)upperbound;

    // Call the visualizer extension
    bh_ext_visualizer(as<float>(ary), args);
}

}
#endif

