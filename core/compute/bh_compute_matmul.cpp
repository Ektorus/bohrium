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
#include <bh.h>
#include <bh_compute.h>


template <typename Type> bh_error do_matmul(bh_view *A, bh_view *B, bh_view *C){

    if(bh_data_malloc(C->base) != BH_SUCCESS)
        return BH_OUT_OF_MEMORY;

    Type* A_data;
    Type* B_data;
    Type* C_data;

    A_data = (Type*)bh_base_array(A)->data;
    B_data = (Type*)bh_base_array(B)->data;
    C_data = (Type*)bh_base_array(C)->data;

    bh_intp M = A->shape[0];
    bh_intp N = B->shape[1];
    bh_intp K = A->shape[1];

    for(bh_intp i = 0; i < M; i++){
        for(bh_intp j = 0; j < N; j++){
            C_data[C->start + i*C->stride[0]+j*C->stride[1]] = 0;
            for(bh_intp k = 0; k < K; k++){
                C_data[C->start + i*C->stride[0]+j*C->stride[1]] += \
                         A_data[A->start + i*A->stride[0]+k*A->stride[1]] * \
                           B_data[B->start + k*B->stride[0]+j*B->stride[1]];
            }
        }
    }

    return BH_SUCCESS;
}



/**
 * bh_compute_matmul
 *
 * Implementation of the user-defined funtion "matmul".
 * Note that we follow the function signature defined by bh_userfunc_impl.
 *
 */
bh_error bh_compute_matmul(bh_userfunc *arg, void* ve_arg)
{
    bh_matmul_type *m_arg = (bh_matmul_type *) arg;
    bh_view *C = &m_arg->operand[0];
    bh_view *A = &m_arg->operand[1];
    bh_view *B = &m_arg->operand[2];

    //Make sure that the arrays memory are allocated.
    if(bh_data_malloc(A->base) != BH_SUCCESS)
        return BH_OUT_OF_MEMORY;
    if(bh_data_malloc(B->base) != BH_SUCCESS)
        return BH_OUT_OF_MEMORY;
    if(bh_data_malloc(C->base) != BH_SUCCESS)
        return BH_OUT_OF_MEMORY;
    
    if (A->base == NULL || B->base == NULL || C->base == NULL)
        return BH_ERROR;

    switch (bh_base_array(C)->type)
    {
    	case BH_INT8:
		    return do_matmul<bh_int8>(A, B, C);
    	case BH_INT16:
		    return do_matmul<bh_int16>(A, B, C);
    	case BH_INT32:
		    return do_matmul<bh_int32>(A, B, C);
    	case BH_INT64:
		    return do_matmul<bh_int64>(A, B, C);
        case BH_UINT8:
		    return do_matmul<bh_uint8>(A, B, C);
    	case BH_UINT16:
		    return do_matmul<bh_uint16>(A, B, C);
    	case BH_UINT32:
	        return do_matmul<bh_uint32>(A, B, C);
    	case BH_UINT64:
		    return do_matmul<bh_uint64>(A, B, C);
    	case BH_FLOAT32:
		    return do_matmul<bh_float32>(A, B, C);
    	case BH_FLOAT64:
		    return do_matmul<bh_float64>(A, B, C);
        default:
            return BH_TYPE_NOT_SUPPORTED;

	}

}



