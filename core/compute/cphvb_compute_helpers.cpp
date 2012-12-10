/*
This file is part of cphVB and copyright (c) 2012 the cphVB team:
http://cphvb.bitbucket.org

cphVB is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as 
published by the Free Software Foundation, either version 3 
of the License, or (at your option) any later version.

cphVB is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the 
GNU Lesser General Public License along with cphVB. 

If not, see <http://www.gnu.org/licenses/>.
*/
#include <assert.h>
#include <cphvb.h>
#include <cphvb_compute.h>
#include "cphvb_compute_iterator.hpp"

//This function protects against arrays that have superfluos dimensions,
// which could negatively affect computation speed
void cphvb_compact_dimensions(cphvb_tstate* state)
{
	cphvb_index i, j;
	
	//As long as the inner dimension has length == 1, we can safely ignore it
	while(state->ndim > 1 && (state->shape[state->ndim - 1] == 1 || state->shape[state->ndim - 1] == 0))
		state->ndim--;

	//We can also remove dimensions inside the seqence
	for(i = state->ndim-2; i >= 0; i--) 
	{
		if (state->shape[i] == 1 || state->shape[i] == 0) 
		{
			state->ndim--;
			memmove(&state->shape[i], &state->shape[i+1], (state->ndim - i) * sizeof(cphvb_index));
			for(j = 0; j < state->noperands; j++)
				memmove(&state->stride[j][i], &state->stride[j][i+1], (state->ndim - i) * sizeof(cphvb_index));
		}
	}
}

void cphvb_tstate_reset( cphvb_tstate *state, cphvb_instruction *instr ) {

	cphvb_index i, j, blocksize, elsize;
	void* basep;
	
	state->ndim         = instr->operand[0]->ndim;
	state->noperands    = cphvb_operands(instr->opcode);
	blocksize 			= state->ndim * sizeof(cphvb_index);

	// As all arrays have the same dimensions, we keep a single shared shape
	memcpy(state->shape, instr->operand[0]->shape, blocksize);

	//Prepare strides for compacting
	for(i = 0; i < state->noperands; i++)
        if (!cphvb_is_constant(instr->operand[i])) 
			memcpy(state->stride[i], instr->operand[i]->stride, blocksize);

	cphvb_compact_dimensions(state);
	
	// Revisit the strides and pre-calculate them for traversal
	for(i = 0; i < state->noperands; i++) 
	{
        if (!cphvb_is_constant(instr->operand[i])) 
        {
        	elsize = cphvb_type_size(instr->operand[i]->type);
        	
        	// Precalculate the pointer
        	basep = cphvb_base_array(instr->operand[i])->data;
        	assert(basep != NULL);
			state->start[i] = (void*)(((char*)basep) + (instr->operand[i]->start * elsize));
			
			// Precalculate the strides in bytes, 
			// relative to the size of the underlying dimension
			for(j = 0; j < state->ndim - 1; j++) {
				state->stride[i][j] = (state->stride[i][j] - (state->stride[i][j+1] * state->shape[j+1])) * elsize;
			}
			state->stride[i][state->ndim - 1] = state->stride[i][state->ndim - 1] * elsize;
		}
	}	
}

/**
 * Execute an instruction using the optimization traversal.
 *
 * @param instr Instruction to execute.
 * @return Status of execution
 */
cphvb_error cphvb_compute_apply( cphvb_instruction *instr ) {

    cphvb_computeloop comp = cphvb_compute_get( instr );
    cphvb_tstate state;
    cphvb_tstate_reset( &state, instr );
    
    if (comp == NULL) {
        return CPHVB_TYPE_NOT_SUPPORTED;
    } else {
        return comp( instr, &state );
    }

}

//
// Below is usage of the naive traversal.
//
void cphvb_tstate_reset_naive( cphvb_tstate_naive *state ) {
    memset(state->coord, 0, CPHVB_MAXDIM * sizeof(cphvb_index));
    state->cur_e = 0;   
}

cphvb_error cphvb_compute_apply_naive( cphvb_instruction *instr ) {

    cphvb_computeloop_naive comp = cphvb_compute_get_naive( instr );
    cphvb_tstate_naive state;
    cphvb_tstate_reset_naive( &state );
    
    if (comp == NULL) {
        return CPHVB_TYPE_NOT_SUPPORTED;
    } else {
        return comp( instr, &state, 0 );
    }

}

cphvb_error cphvb_compute_iterator_apply(cphvb_instruction* instr) {
	cphvb_index i, j;
	cphvb_itstate itstate;
	cphvb_tstate tstate;
	
	cphvb_computeloop_iterator comp;
	
	cphvb_const_iterator cit;
	cphvb_dense_iterator its[CPHVB_MAX_NO_OPERANDS];

	cphvb_index operandcount = cphvb_operands(instr->opcode);
	
	cphvb_tstate_reset(&tstate, instr);

	comp = cphvb_compute_iterator_get(instr, tstate.ndim);
	if (comp == NULL) 
        return CPHVB_TYPE_NOT_SUPPORTED;

	// Reverse order of dimension sizes
	for(i = 0; i < tstate.ndim; i++) {
		itstate.shape[i] = tstate.shape[tstate.ndim - i - 1];
	}
	
	for(i = 0; i < operandcount; i++) {
		if (cphvb_is_constant(instr->operand[i])) {
			cit.start = &instr->constant;
			itstate.iterator[i] = &cit;
		} else {
			its[i].start = tstate.start[i];
			
			// Reverse order of strides
			for(j = 0; j < tstate.ndim; j++) {
				its[i].stride[j] = tstate.stride[i][tstate.ndim - j - 1];
			}
			
			itstate.iterator[i] = &its[i];
		}
	}


	comp(&itstate);
	
	return CPHVB_SUCCESS;
}



