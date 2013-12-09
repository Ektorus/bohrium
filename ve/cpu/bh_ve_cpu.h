/*
This file is part of Bohrium and copyright (c) 2012 the Bohrium
team <http://www.bh107.org>.

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
#ifndef __BH_VE_CPU_H
#define __BH_VE_CPU_H

#include <bh.h>

// Single-Expression-Jit hash: OPCODE_NDIM_LAYOUT_TYPESIG
#define A0_CONSTANT     (1 << 0)
#define A0_CONTIGUOUS   (1 << 1)
#define A0_STRIDED      (1 << 2)
#define A0_SPARSE       (1 << 3)

#define A1_CONSTANT     (1 << 4)
#define A1_CONTIGUOUS   (1 << 5)
#define A1_STRIDED      (1 << 6)
#define A1_SPARSE       (1 << 7)

#define A2_CONSTANT     (1 << 8)
#define A2_CONTIGUOUS   (1 << 9)
#define A2_STRIDED      (1 << 10)
#define A2_SPARSE       (1 << 11)

#ifdef __cplusplus
extern "C" {
#endif

/* Component interface: init (see bh_component.h) */
DLLEXPORT bh_error bh_ve_cpu_init(const char *name);

/* Component interface: execute (see bh_component.h) */
DLLEXPORT bh_error bh_ve_cpu_execute(bh_ir* bhir);

/* Component interface: shutdown (see bh_component.h) */
DLLEXPORT bh_error bh_ve_cpu_shutdown(void);

/* Component interface: extmethod (see bh_component.h) */
DLLEXPORT bh_error bh_ve_cpu_extmethod(const char *name, bh_opcode opcode);

#ifdef __cplusplus
}
#endif

#endif
