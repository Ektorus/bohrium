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

/**
 * Execute an instruction using the iterator traversal.
 *
 * @param instr Instruction to execute.
 * @return Status of execution
 */
cphvb_error cphvb_compute_iterator_apply( cphvb_instruction *instr ) {

	cphvb_index i;
	void* args[3];
	cphvb_dense_iterator dit;
	cphvb_constant_iterator cit;

	cphvb_computeloop_iterator comp = cphvb_compute_iterator_get(instr);
    if (comp == NULL)
        return CPHVB_TYPE_NOT_SUPPORTED;
	
	cphvb_dense_iterator_reset(&dit, instr);	
	args[0] = &dit;
	
	// If we have a constant, pass a constant iterator as well
	if (cphvb_is_constant(instr->operand[1]) || (cphvb_operands(instr->opcode) == 3 && cphvb_is_constant(instr->operand[2]))) {
		cphvb_constant_iterator_reset(&cit, instr);
		args[1] = &cit;
	}

	cphvb_index ops = 1;
	for(i = 0; i < instr->operand[0]->ndim; i++)
		ops *= instr->operand[0]->shape[i];

	return comp(ops, (void**)&args);
	
}

/**
 * Execute an instruction using the iterator traversal.
 *
 * @param it Iterator to reset.
 * @param instr Instruction to setup iterator for.
 * @return Status of reset
 */
cphvb_error cphvb_dense_iterator_reset(cphvb_dense_iterator* it, cphvb_instruction* instr)
{
	cphvb_index i;
	cphvb_tstate tstate;
	
	cphvb_index operandcount = cphvb_operands(instr->opcode);

	//We need mostly whatever tstate has
	cphvb_tstate_reset(&tstate, instr);
	memcpy(it->stride, tstate.stride, sizeof(cphvb_index) * CPHVB_MAXDIM * operandcount);
	memcpy(it->start, tstate.start, sizeof(void*) * operandcount);
	memcpy(it->shape, tstate.shape, sizeof(cphvb_index) * tstate.ndim);

	//We count down, because we can compare with 0 instead of a value lookup
	memcpy(it->counters, tstate.shape, sizeof(cphvb_index) * tstate.ndim);
	
	cphvb_index lastdim = tstate.ndim - 1;
	it->pstart = tstate.ndim - 4;
	it->inner_ops = 0;
	
	// Special case, the middle operand is constant, so we
	// rearrange the rightmost operand to use the middle index.
	// This removes the need for a special next iterator
	if (operandcount == 3 && cphvb_is_constant(instr->operand[1]))
	{
		it->start[1] = it->start[2];
		memcpy(it->stride[1], it->stride[2], sizeof(cphvb_index) * CPHVB_MAXDIM);
		operandcount--;
	}
	
	// Prepare 2-bit inner lookup tables
	for(i = 0; i < operandcount; i++) {
		it->stride_lookup[0][i] = it->stride[i][lastdim];
		if (lastdim == 0) {
			it->stride_lookup[1][i] = it->stride[i][lastdim];
			it->stride_lookup[2][i] = it->stride[i][lastdim];
			it->stride_lookup[3][i] = it->stride[i][lastdim];
		} else if (lastdim == 1) {
			it->stride_lookup[1][i] = it->stride[i][lastdim] + it->stride[i][lastdim - 1];
			it->stride_lookup[2][i] = it->stride[i][lastdim];
			it->stride_lookup[3][i] = it->stride[i][lastdim] + it->stride[i][lastdim - 1];
		} else {
			it->stride_lookup[1][i] = it->stride[i][lastdim] + it->stride[i][lastdim - 1];
			it->stride_lookup[2][i] = it->stride[i][lastdim] + it->stride[i][lastdim - 2];
			it->stride_lookup[3][i] = it->stride[i][lastdim] + it->stride[i][lastdim - 1] + it->stride[i][lastdim - 2];
		}
	}

	// Calculate the sizes of the inner dimensions
	it->shapelimit2 = it->shape[lastdim];
	if (lastdim == 0) {
		it->shapelimit1 = -1;
		it->shapelimit0 = -1;
	} else if (lastdim == 1) {
		it->shapelimit1 = it->shapelimit2 * it->shape[lastdim - 1];
		it->shapelimit0 = -1;
	} else {
		it->shapelimit1 = it->shapelimit2 * it->shape[lastdim - 1];
		it->shapelimit0 = it->shapelimit1 * it->shape[lastdim - 2];
	}
		
	return CPHVB_SUCCESS;
}

/**
 * Execute an instruction using the iterator traversal.
 *
 * @param it Iterator to reset.
 * @param instr Instruction to setup iterator for.
 * @return Status of reset
 */
cphvb_error cphvb_constant_iterator_reset(cphvb_constant_iterator* it, cphvb_instruction* instr)
{
	cphvb_index i, noperands;
	noperands = cphvb_operands(instr->opcode);
	
	for(i = 0; i < noperands; i++)
        if (cphvb_is_constant(instr->operand[i])) {
        	it->start[0] = &instr->constant.value;
        	return CPHVB_SUCCESS;
        }

	return CPHVB_ERROR;
}