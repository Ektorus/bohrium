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
#include <string>
#include <sstream>
#include <vector>
#include <set>
#include <stdexcept>
#if defined __GNUC__ || defined __APPLE__
#include <tr1/unordered_map>
#else
#include <unordered_map>
#endif
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <ctemplate/template.h>  
#include <bh.h>
#include <bh_vcache.h>
#include "bh_ve_cpu.h"
#include "compiler.cpp"

// Execution Profile

#ifdef PROFILE
static bh_uint64 times[BH_NO_OPCODES+2]; // opcodes and: +1=malloc, +2=kernel
static bh_uint64 calls[BH_NO_OPCODES+2];
#endif

static bh_component *myself = NULL;
static bh_userfunc_impl random_impl = NULL;
static bh_intp random_impl_id = 0;
static bh_userfunc_impl matmul_impl = NULL;
static bh_intp matmul_impl_id = 0;
static bh_userfunc_impl nselect_impl = NULL;
static bh_intp nselect_impl_id = 0;

static bh_intp vcache_size   = 10;
static bh_intp do_fuse = 1;
static bh_intp do_jit  = 1;
static bh_intp dump_src = 0;

static char* compiler_cmd;   // cpu Arguments
static char* kernel_path;
static char* object_path;
static char* template_path;

process* target;

typedef struct bh_sij {     
    bh_instruction *instr;  // Pointer to instruction
    int64_t ndims;          // Number of dimensions
    int lmask;              // Layout mask
    int tsig;               // Type signature

    std::string symbol;     // String representation
} bh_sij_t;                 // Encapsulation of single-instruction(expression)-jit

void bh_string_option(char *&option, const char *env_name, const char *conf_name)
{
    option = getenv(env_name);           // For the compiler
    if (NULL==option) {
        option = bh_component_config_lookup(myself, conf_name);
    }
    char err_msg[100];

    if (!option) {
        sprintf(err_msg, "Err: String is not set; option (%s).\n", conf_name);
        throw std::runtime_error(err_msg);
    }
}

void bh_path_option(char *&option, const char *env_name, const char *conf_name)
{
    option = getenv(env_name);           // For the compiler
    if (NULL==option) {
        option = bh_component_config_lookup(myself, conf_name);
    }
    char err_msg[100];

    if (!option) {
        sprintf(err_msg, "Err: Path is not set; option (%s).\n", conf_name);
        throw std::runtime_error(err_msg);
    }
    if (0 != access(option, F_OK)) {
        if (ENOENT == errno) {
            sprintf(err_msg, "Err: Path does not exist; path (%s).\n", option);
        } else if (ENOTDIR == errno) {
            sprintf(err_msg, "Err: Path is not a directory; path (%s).\n", option);
        } else {
            sprintf(err_msg, "Err: Path is broken somehow; path (%s).\n", option);
        }
        throw std::runtime_error(err_msg);
    }
}

bh_error bh_ve_cpu_init(bh_component *self)
{
    myself = self;
    char *env;

    env = getenv("BH_CORE_VCACHE_SIZE");      // Override block_size from environment-variable.
    if (NULL != env) {
        vcache_size = atoi(env);
    }
    if (0 > vcache_size) {                          // Verify it
        fprintf(stderr, "BH_CORE_VCACHE_SIZE (%ld) should be greater than zero!\n", (long int)vcache_size);
        return BH_ERROR;
    }

    env = getenv("BH_VE_CPU_DOFUSE");
    if (NULL != env) {
        do_fuse = atoi(env);
    }
    if (!((0==do_fuse) || (1==do_fuse))) {
        fprintf(stderr, "BH_VE_CPU_DOFUSE (%ld) should 0 or 1.\n", (long int)do_fuse);
        return BH_ERROR;   
    }

    env = getenv("BH_VE_CPU_DUMPSRC");
    if (NULL != env) {
        dump_src = atoi(env);
    }
    if (!((0==dump_src) || (1==dump_src))) {
         fprintf(stderr, "BH_VE_CPU_DUMPSRC (%ld) should 0 or 1.\n", (long int)dump_src);
        return BH_ERROR;   
    }

    bh_vcache_init(vcache_size);

    // CPU Arguments
    bh_path_option(     kernel_path,    "BH_VE_CPU_KERNEL_PATH",   "kernel_path");
    bh_path_option(     object_path,    "BH_VE_CPU_OBJECT_PATH",   "object_path");
    bh_path_option(     template_path,  "BH_VE_CPU_TEMPLATE_PATH", "template_path");
    bh_string_option(   compiler_cmd,   "BH_VE_CPU_COMPILER",      "compiler_cmd");

    target = new process(compiler_cmd, object_path, kernel_path, true);

    #ifdef PROFILE
    memset(&times, 0, sizeof(bh_uint64)*(BH_NO_OPCODES+2));
    memset(&calls, 0, sizeof(bh_uint64)*(BH_NO_OPCODES+2));
    #endif

    return BH_SUCCESS;
}

void symbolize(bh_instruction *instr, bh_sij_t &sij) {

    char symbol_c[500];             // String representation buffers
    char dims_str[10];

    bh_random_type *random_args;    // Nescesarry evil! Until random becomes
                                    // an opcode and not an extension

    sij.instr = instr;
    switch (sij.instr->opcode) {                    // [OPCODE_SWITCH]

        case BH_NONE:                           // System opcodes
        case BH_DISCARD:
        case BH_SYNC:
        case BH_FREE:
            break;

        case BH_USERFUNC:                       // Extensions

            if (sij.instr->userfunc->id == random_impl_id) {
                random_args = (bh_random_type*)sij.instr->userfunc;
                sprintf(
                    symbol_c,
                    "BH_RANDOM_D_%s",
                    bhtype_to_shorthand(random_args->operand[0].base->type)
                );

                sij.symbol = std::string(symbol_c);
                sij.tsig   = random_args->operand[0].base->type+1;
                sij.ndims  = 1;
            }
            break;

        case BH_ADD_REDUCE:                             // Reductions
        case BH_MULTIPLY_REDUCE:
        case BH_MINIMUM_REDUCE:
        case BH_MAXIMUM_REDUCE:
        case BH_LOGICAL_AND_REDUCE:
        case BH_BITWISE_AND_REDUCE:
        case BH_LOGICAL_OR_REDUCE:
        case BH_LOGICAL_XOR_REDUCE:
        case BH_BITWISE_OR_REDUCE:
        case BH_BITWISE_XOR_REDUCE:
            sij.ndims = sij.instr->operand[1].ndim;     // Dimensions
            sij.lmask = bh_layoutmask(sij.instr);       // Layout mask
            sij.tsig  = bh_typesig(sij.instr);          // Type signature

            if (sij.ndims <= 3) {                       // String representation
                sprintf(dims_str, "%lldD", sij.ndims);
            } else {
                sprintf(dims_str, "ND");
            }
            sprintf(symbol_c, "%s_%s_%s_%s",
                bh_opcode_text(sij.instr->opcode),
                dims_str,
                bh_layoutmask_to_shorthand(sij.lmask),
                bh_typesig_to_shorthand(sij.tsig)
            );

            sij.symbol = std::string(symbol_c);
            break;


        default:                                        // Built-in
            sij.ndims = sij.instr->operand[0].ndim;     // Dimensions
            sij.lmask = bh_layoutmask(sij.instr);       // Layout mask
            sij.tsig  = bh_typesig(sij.instr);          // Type signature

            if (sij.ndims <= 3) {                       // String representation
                sprintf(dims_str, "%lldD", sij.ndims);
            } else {
                sprintf(dims_str, "ND");
            }
            sprintf(symbol_c, "%s_%s_%s_%s",
                bh_opcode_text(sij.instr->opcode),
                dims_str,
                bh_layoutmask_to_shorthand(sij.lmask),
                bh_typesig_to_shorthand(sij.tsig)
            );

            sij.symbol = std::string(symbol_c);
            break;
    }
}

std::string specialize(bh_sij_t &sij) {

    bh_random_type *random_args;
    
    char template_fn[500];   // NOTE: constants like these are often traumatizing!

    bool cres = false;

    ctemplate::TemplateDictionary dict("codegen");
    dict.ShowSection("include");

    //dict.ShowSection("license");

    switch (sij.instr->opcode) {                    // OPCODE_SWITCH

        case BH_USERFUNC:                       // Extensions
            if (sij.instr->userfunc->id == random_impl_id) {
                random_args = (bh_random_type*)sij.instr->userfunc;
                dict.SetValue("SYMBOL",     sij.symbol);
                dict.SetValue("TYPE_A0",    bhtype_to_ctype(random_args->operand[0].base->type));
                dict.SetValue("TYPE_A0_SHORTHAND", bhtype_to_shorthand(random_args->operand[0].base->type));
                sprintf(template_fn, "%s/random.tpl", template_path);

                cres = true;
            } 
            break;
        
        case BH_ADD_REDUCE:
        case BH_MULTIPLY_REDUCE:
        case BH_MINIMUM_REDUCE:
        case BH_MAXIMUM_REDUCE:
        case BH_LOGICAL_AND_REDUCE:
        case BH_BITWISE_AND_REDUCE:
        case BH_LOGICAL_OR_REDUCE:
        case BH_LOGICAL_XOR_REDUCE:
        case BH_BITWISE_OR_REDUCE:
        case BH_BITWISE_XOR_REDUCE:

            dict.SetValue("OPERATOR", bhopcode_to_cexpr(sij.instr->opcode));
            dict.SetValue("SYMBOL", sij.symbol);
            dict.SetValue("TYPE_A0", bhtype_to_ctype(sij.instr->operand[0].base->type));
            dict.SetValue("TYPE_A1", bhtype_to_ctype(sij.instr->operand[1].base->type));

            if (sij.ndims <= 3) {
                sprintf(template_fn, "%s/reduction.%lldd.tpl", template_path, sij.ndims);
            } else {
                sprintf(template_fn, "%s/reduction.nd.tpl", template_path);
            }

            cres = true;
            break;

        case BH_ADD:
        case BH_SUBTRACT:
        case BH_MULTIPLY:
        case BH_DIVIDE:
        case BH_POWER:
        case BH_GREATER:
        case BH_GREATER_EQUAL:
        case BH_LESS:
        case BH_LESS_EQUAL:
        case BH_EQUAL:
        case BH_NOT_EQUAL:
        case BH_LOGICAL_AND:
        case BH_LOGICAL_OR:
        case BH_LOGICAL_XOR:
        case BH_MAXIMUM:
        case BH_MINIMUM:
        case BH_BITWISE_AND:
        case BH_BITWISE_OR:
        case BH_BITWISE_XOR:
        case BH_LEFT_SHIFT:
        case BH_RIGHT_SHIFT:
        case BH_ARCTAN2:
        case BH_MOD:

            dict.SetValue("OPERATOR", bhopcode_to_cexpr(sij.instr->opcode));
            dict.ShowSection("binary");
            if (bh_is_constant(&sij.instr->operand[2])) {
                dict.SetValue("SYMBOL", sij.symbol);
                dict.SetValue("TYPE_A0", bhtype_to_ctype(sij.instr->operand[0].base->type));
                dict.SetValue("TYPE_A1", bhtype_to_ctype(sij.instr->operand[1].base->type));
                dict.SetValue("TYPE_A2", bhtype_to_ctype(sij.instr->constant.type));
                dict.ShowSection("a1_dense");
                dict.ShowSection("a2_scalar");
            } else if (bh_is_constant(&sij.instr->operand[1])) {
                dict.SetValue("SYMBOL", sij.symbol);
                dict.SetValue("TYPE_A0", bhtype_to_ctype(sij.instr->operand[0].base->type));
                dict.SetValue("TYPE_A1", bhtype_to_ctype(sij.instr->constant.type));
                dict.SetValue("TYPE_A2", bhtype_to_ctype(sij.instr->operand[2].base->type));
                dict.ShowSection("a1_scalar");
                dict.ShowSection("a2_dense");
            } else {
                dict.SetValue("SYMBOL", sij.symbol);
                dict.SetValue("TYPE_A0", bhtype_to_ctype(sij.instr->operand[0].base->type));
                dict.SetValue("TYPE_A1", bhtype_to_ctype(sij.instr->operand[1].base->type));
                dict.SetValue("TYPE_A2", bhtype_to_ctype(sij.instr->operand[2].base->type));
                dict.ShowSection("a1_dense");
                dict.ShowSection("a2_dense");

            }
            if (sij.ndims<=3) {
                sprintf(template_fn, "%s/traverse.%lldd.tpl", template_path, sij.ndims);
            } else {
                sprintf(template_fn, "%s/traverse.nd.tpl", template_path);
            }

            cres = true;
            break;

        case BH_ABSOLUTE:
        case BH_LOGICAL_NOT:
        case BH_INVERT:
        case BH_COS:
        case BH_SIN:
        case BH_TAN:
        case BH_COSH:
        case BH_SINH:
        case BH_TANH:
        case BH_ARCSIN:
        case BH_ARCCOS:
        case BH_ARCTAN:
        case BH_ARCSINH:
        case BH_ARCCOSH:
        case BH_ARCTANH:
        case BH_EXP:
        case BH_EXP2:
        case BH_EXPM1:
        case BH_LOG:
        case BH_LOG2:
        case BH_LOG10:
        case BH_LOG1P:
        case BH_SQRT:
        case BH_CEIL:
        case BH_TRUNC:
        case BH_FLOOR:
        case BH_RINT:
        case BH_ISNAN:
        case BH_ISINF:
        case BH_IDENTITY:

            dict.SetValue("OPERATOR", bhopcode_to_cexpr(sij.instr->opcode));
            dict.ShowSection("unary");
            if (bh_is_constant(&sij.instr->operand[1])) {
                dict.SetValue("SYMBOL", sij.symbol);
                dict.SetValue("TYPE_A0", bhtype_to_ctype(sij.instr->operand[0].base->type));
                dict.SetValue("TYPE_A1", bhtype_to_ctype(sij.instr->constant.type));
                dict.ShowSection("a1_scalar");
            } else {
                dict.SetValue("SYMBOL", sij.symbol);
                dict.SetValue("TYPE_A0", bhtype_to_ctype(sij.instr->operand[0].base->type));
                dict.SetValue("TYPE_A1", bhtype_to_ctype(sij.instr->operand[1].base->type));
                dict.ShowSection("a1_dense");
            } 
            if (sij.ndims<=3) {
                sprintf(template_fn, "%s/traverse.%lldd.tpl", template_path, sij.ndims);
            } else {
                sprintf(template_fn, "%s/traverse.nd.tpl", template_path);
            }

            cres = true;
            break;

        default:
            printf("cpu: Err=[Unsupported ufunc...]\n");
    }

    if (!cres) {
        throw std::runtime_error("cpu: Failed specializing code.");
    }

    std::string sourcecode = "";
    ctemplate::ExpandTemplate(
        template_fn,
        ctemplate::STRIP_BLANK_LINES,
        &dict,
        &sourcecode
    );

    return sourcecode;
}

bh_error bh_ve_cpu_execute(bh_ir* bhir)
{
    #ifdef PROFILE
    bh_uint64 t_begin, t_end, m_begin, m_end;
    #endif

    bh_error res = BH_SUCCESS;

    for(bh_intp i=0; i<bhir->ninstr; ++i) {

        bh_random_type *random_args;
        bh_sij_t        sij;

        #ifdef PROFILE
        t_begin = _bh_timing();
        m_begin = _bh_timing();
        t_end=0, m_end=0;
        #endif

        symbolize(&bhir->instr_list[i], sij);           // Grab the symbol / IR-HASH
        if (do_jit && (sij.symbol!="") && (!target->symbol_ready(sij.symbol))) {
            std::string sourcecode = specialize(sij);   // Specialize sourcecode
            if (dump_src==1) {                          // Dump sourcecode to file
                target->src_to_file(sij.symbol, sourcecode.c_str(), sourcecode.size());
            }                                           // Send to compiler / cache
            target->compile(sij.symbol, sourcecode.c_str(), sourcecode.size()); 
        }
        if ((sij.symbol!="") && !target->load(sij.symbol, sij.symbol)) {    // Load
            return BH_ERROR;
        }
        res = bh_vcache_malloc(sij.instr);                          // Allocate memory for operands
        if (BH_SUCCESS != res) {
            fprintf(stderr, "Unhandled error returned by bh_vcache_malloc() "
                            "called from bh_ve_cpu_execute()\n");
            return res;
        }

        switch (sij.instr->opcode) {    // OPCODE_SWITCH

            case BH_NONE:                           // NOOP.
            case BH_DISCARD:
            case BH_SYNC:
                res = BH_SUCCESS;
                break;

            case BH_FREE:                           // Store data-pointer in malloc-cache
                res = bh_vcache_free(sij.instr);
                break;

            case BH_USERFUNC:
                if (sij.instr->userfunc->id == random_impl_id) { // RANDOM!

                    random_args = (bh_random_type*)sij.instr->userfunc;
                    #ifdef PROFILE
                    m_begin = _bh_timing();
                    #endif
                    if (BH_SUCCESS != bh_vcache_malloc_op(&random_args->operand[0])) {
                        std::cout << "SHIT HIT THE FAN" << std::endl;
                    }
                    #ifdef PROFILE
                    m_end = _bh_timing();
                    times[BH_NO_OPCODES] += m_end-m_begin;
                    ++calls[BH_NO_OPCODES];
                    #endif

                    // De-assemble the RANDOM_UFUNC     // CALL
                    target->funcs[sij.symbol](0,
                        bh_base_array(&random_args->operand[0])->data,
                        bh_nelements(random_args->operand[0].ndim, random_args->operand[0].shape)
                    );
                    res = BH_SUCCESS;

                } else if(sij.instr->userfunc->id == matmul_impl_id) {
                    res = matmul_impl(sij.instr->userfunc, NULL);
                } else if(sij.instr->userfunc->id == nselect_impl_id) {
                    res = nselect_impl(sij.instr->userfunc, NULL);
                } else {                            // Unsupported userfunc
                    res = BH_USERFUNC_NOT_SUPPORTED;
                }

                break;

            case BH_ADD_REDUCE:                     // Partial Reductions
            case BH_MULTIPLY_REDUCE:
            case BH_MINIMUM_REDUCE:
            case BH_MAXIMUM_REDUCE:
            case BH_LOGICAL_AND_REDUCE:
            case BH_BITWISE_AND_REDUCE:
            case BH_LOGICAL_OR_REDUCE:
            case BH_LOGICAL_XOR_REDUCE:
            case BH_BITWISE_OR_REDUCE:
            case BH_BITWISE_XOR_REDUCE:

                target->funcs[sij.symbol](0,
                    bh_base_array(&sij.instr->operand[0])->data,
                    sij.instr->operand[0].start,
                    sij.instr->operand[0].stride,
                    sij.instr->operand[0].shape,
                    sij.instr->operand[0].ndim,

                    bh_base_array(&sij.instr->operand[1])->data,
                    sij.instr->operand[1].start,
                    sij.instr->operand[1].stride,
                    sij.instr->operand[1].shape,
                    sij.instr->operand[1].ndim,

                    sij.instr->constant.value
                );
                res = BH_SUCCESS;
                break;

            case BH_ADD:
            case BH_SUBTRACT:
            case BH_MULTIPLY:
            case BH_DIVIDE:
            case BH_POWER:
            case BH_GREATER:
            case BH_GREATER_EQUAL:
            case BH_LESS:
            case BH_LESS_EQUAL:
            case BH_EQUAL:
            case BH_NOT_EQUAL:
            case BH_LOGICAL_AND:
            case BH_LOGICAL_OR:
            case BH_LOGICAL_XOR:
            case BH_MAXIMUM:
            case BH_MINIMUM:
            case BH_BITWISE_AND:
            case BH_BITWISE_OR:
            case BH_BITWISE_XOR:
            case BH_LEFT_SHIFT:
            case BH_RIGHT_SHIFT:
            case BH_ARCTAN2:
            case BH_MOD:

                if (bh_is_constant(&sij.instr->operand[2])) {         // DDC
                    target->funcs[sij.symbol](0,
                        bh_base_array(&sij.instr->operand[0])->data,
                        sij.instr->operand[0].start,
                        sij.instr->operand[0].stride,

                        bh_base_array(&sij.instr->operand[1])->data,
                        sij.instr->operand[1].start,
                        sij.instr->operand[1].stride,

                        &(sij.instr->constant.value),

                        sij.instr->operand[0].shape,
                        sij.instr->operand[0].ndim
                    );
                } else if (bh_is_constant(&sij.instr->operand[1])) {  // DCD
                    target->funcs[sij.symbol](0,
                        bh_base_array(&sij.instr->operand[0])->data,
                        sij.instr->operand[0].start,
                        sij.instr->operand[0].stride,

                        &(sij.instr->constant.value),

                        bh_base_array(&sij.instr->operand[2])->data,
                        sij.instr->operand[2].start,
                        sij.instr->operand[2].stride,

                        sij.instr->operand[0].shape,
                        sij.instr->operand[0].ndim
                    );
                } else {                                        // DDD
                    target->funcs[sij.symbol](0,
                        bh_base_array(&sij.instr->operand[0])->data,
                        sij.instr->operand[0].start,
                        sij.instr->operand[0].stride,

                        bh_base_array(&sij.instr->operand[1])->data,
                        sij.instr->operand[1].start,
                        sij.instr->operand[1].stride,

                        bh_base_array(&sij.instr->operand[2])->data,
                        sij.instr->operand[2].start,
                        sij.instr->operand[2].stride,

                        sij.instr->operand[0].shape,
                        sij.instr->operand[0].ndim
                    );
                }

                res = BH_SUCCESS;
                break;

            case BH_ABSOLUTE:
            case BH_LOGICAL_NOT:
            case BH_INVERT:
            case BH_COS:
            case BH_SIN:
            case BH_TAN:
            case BH_COSH:
            case BH_SINH:
            case BH_TANH:
            case BH_ARCSIN:
            case BH_ARCCOS:
            case BH_ARCTAN:
            case BH_ARCSINH:
            case BH_ARCCOSH:
            case BH_ARCTANH:
            case BH_EXP:
            case BH_EXP2:
            case BH_EXPM1:
            case BH_LOG:
            case BH_LOG2:
            case BH_LOG10:
            case BH_LOG1P:
            case BH_SQRT:
            case BH_CEIL:
            case BH_TRUNC:
            case BH_FLOOR:
            case BH_RINT:
            case BH_ISNAN:
            case BH_ISINF:
            case BH_IDENTITY:
            
                if (bh_is_constant(&sij.instr->operand[1])) {
                    target->funcs[sij.symbol](0,
                        bh_base_array(&sij.instr->operand[0])->data,
                        sij.instr->operand[0].start,
                        sij.instr->operand[0].stride,

                        &(sij.instr->constant.value),

                        sij.instr->operand[0].shape,
                        sij.instr->operand[0].ndim
                    );
                } else {
                    target->funcs[sij.symbol](0,
                        bh_base_array(&sij.instr->operand[0])->data,
                        sij.instr->operand[0].start,
                        sij.instr->operand[0].stride,

                        bh_base_array(&sij.instr->operand[1])->data,
                        sij.instr->operand[1].start,
                        sij.instr->operand[1].stride,

                        sij.instr->operand[0].shape,
                        sij.instr->operand[0].ndim
                    );
                }
                res = BH_SUCCESS;
                break;

            default:                            // Shit hit the fan
                res = BH_ERROR;
                printf("cpu: Err=[Unsupported ufunc...\n");
        }

        #ifdef PROFILE
        m_end = _bh_timing();
        times[BH_NO_OPCODES] += m_end-m_begin;
        ++calls[BH_NO_OPCODES];
        #endif

        if (BH_SUCCESS != res) {    // Instruction failed
            break;
        }
        #ifdef PROFILE
        t_end = _bh_timing();
        times[sij.instr->opcode] += (t_end-t_begin)+ (m_end-m_begin);
        ++calls[sij.instr->opcode];
        #endif
    }

	return res;
}

bh_error bh_ve_cpu_shutdown(void)
{
    if (vcache_size>0) {
        bh_vcache_clear();  // De-allocate the malloc-cache
        bh_vcache_delete();
    }

    delete target;  // De-allocate code-generator

    #ifdef PROFILE
    bh_uint64 sum = 0;
    for(size_t i=0; i<BH_NO_OPCODES; ++i) {
        if (times[i]>0) {
            sum += times[i];
            printf(
                "%s, %ld, %f\n",
                bh_opcode_text(i), calls[i], (times[i]/1000000.0)
            );
        }
    }
    if (calls[BH_NO_OPCODES]>0) {
        sum += times[BH_NO_OPCODES];
        printf(
            "%s, %ld, %f\n",
            "Memory", calls[BH_NO_OPCODES], (times[BH_NO_OPCODES]/1000000.0)
        );
    }
    if (calls[BH_NO_OPCODES+1]>0) {
        sum += times[BH_NO_OPCODES+1];
        printf(
            "%s, %ld, %f\n",
            "Kernels", calls[BH_NO_OPCODES+1], (times[BH_NO_OPCODES+1]/1000000.0)
        );
    }
    printf("TOTAL, %f\n", sum/1000000.0);
    #endif

    return BH_SUCCESS;
}

bh_error bh_ve_cpu_reg_func(char *fun, bh_intp *id) 
{
    if (strcmp("bh_random", fun) == 0) {
    	if (random_impl == NULL) {
            random_impl_id = *id;
            return BH_SUCCESS;			
        } else {
        	*id = random_impl_id;
        	return BH_SUCCESS;
        }
    } else if (strcmp("bh_matmul", fun) == 0) {
    	if (matmul_impl == NULL) {
            bh_component_get_func(myself, fun, &matmul_impl);
            if (matmul_impl == NULL) {
                return BH_USERFUNC_NOT_SUPPORTED;
            }
            
            matmul_impl_id = *id;
            return BH_SUCCESS;			
        } else {
        	*id = matmul_impl_id;
        	return BH_SUCCESS;
        }
    } else if(strcmp("bh_nselect", fun) == 0) {
        if (nselect_impl == NULL) {
            bh_component_get_func(myself, fun, &nselect_impl);
            if (nselect_impl == NULL) {
                return BH_USERFUNC_NOT_SUPPORTED;
            }
            nselect_impl_id = *id;
            return BH_SUCCESS;
        } else {
            *id = nselect_impl_id;
            return BH_SUCCESS;
        }
    }
        
    return BH_USERFUNC_NOT_SUPPORTED;
}

bh_error bh_matmul( bh_userfunc *arg, void* ve_arg)
{
    return bh_compute_matmul( arg, ve_arg );
}

bh_error bh_nselect( bh_userfunc *arg, void* ve_arg)
{
    return bh_compute_nselect( arg, ve_arg );
}

