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
#include <cphvb.h>
#include "cphvb_ve_jit2.h"
#include "cphvb_mcache.h"

#include "jit_codegenerator.h"
#include "jit_ast.h"
#include "jit_analyser.h"
#include "jit_compile.h"
#include "jit_ssa_analyser.h"
#include "jit_logging.h"
#include "jit_common.h"
#include "StringHasher.hpp"

#include <iostream>
#include <sstream>
#include <set>

#include "cphvb_array.h"
#include "cphvb_type.h"
#include "cphvb.h"
#include <cmath>

static cphvb_component *myself = NULL;
static cphvb_userfunc_impl reduce_impl = NULL;
static cphvb_intp reduce_impl_id = 0;
static cphvb_userfunc_impl random_impl = NULL;
static cphvb_intp random_impl_id = 0;
static cphvb_userfunc_impl matmul_impl = NULL;
static cphvb_intp matmul_impl_id = 0;

jit_ssa_map* jitssamap;
jit_name_table* jitnametable;
jit_execute_list* jitexecutelist;
cphvb_instruction* jit_prev_instruction;
jit_base_dependency_table* jitbasedeptable;

using namespace std;

cphvb_error cphvb_ve_jit_init(cphvb_component *self)
{
    myself = self;
    cphvb_mcache_init( 10 );        
    jitssamap = new jit_ssa_map();
    jitnametable = new jit_name_table();
    jitexecutelist = new jit_execute_list(); 
    jitbasedeptable = new jit_base_dependency_table();
    jit_prev_instruction = NULL;
    
    return CPHVB_SUCCESS;
}

cphvb_error execute_simple(cphvb_intp instruction_count, cphvb_instruction* instruction_list) {
    cphvb_intp count;
    cphvb_instruction* inst;
    cphvb_error res;

    for (count=0; count < instruction_count; count++) {

        inst = &instruction_list[count];
        if (inst->status == CPHVB_SUCCESS) {        // SKIP instruction
            continue;
        }

        res = cphvb_mcache_malloc( inst );          // Allocate memory for operands
        if ( res != CPHVB_SUCCESS ) {
            printf("Unhandled error returned by cphvb_mcache_malloc() called from cphvb_ve_simple_execute()\n");
            return res;
        }
                                                    
        switch (inst->opcode) {                     // Dispatch instruction

            case CPHVB_NONE:                        // NOOP.
            case CPHVB_DISCARD:
            case CPHVB_SYNC:
                inst->status = CPHVB_SUCCESS;
                break;
            case CPHVB_FREE:                        // Store data-pointer in malloc-cache
                inst->status = cphvb_mcache_free( inst );
                break;

            case CPHVB_USERFUNC:                    // External libraries

                if (inst->userfunc->id == reduce_impl_id) {

                    inst->status = reduce_impl(inst->userfunc, NULL);

                } else if(inst->userfunc->id == random_impl_id) {

                    inst->status = random_impl(inst->userfunc, NULL);

                } else if(inst->userfunc->id == matmul_impl_id) {

                    inst->status = matmul_impl(inst->userfunc, NULL);

                } else {                            // Unsupported userfunc
                
                    inst->status = CPHVB_USERFUNC_NOT_SUPPORTED;
                }

                break;

            default:                            // Built-in operations                                        
                inst->status = cphvb_compute_apply( inst );

        }

        if (inst->status != CPHVB_SUCCESS) {    // Instruction failed
            break;
        }
    }

    if (count == instruction_count) {
        return CPHVB_SUCCESS;
    } else {
        return CPHVB_PARTIAL_SUCCESS;
    }
}

void get_expr_base_dependencies(std::vector<cphvb_intp>* list, std::vector<cphvb_intp>* dep_list, cphvb_intp name) {     
    int i, list_size = (int) list->size();
    for(i=0; i<list_size; i++) {
        printf("%d  , n: %ld \n",i,name);

        // when element i is greater the Name, the rest will not be required by Name.
        if (list->at(i) < name) {
            dep_list->push_back(list->at(i));
            //std::vector<cphvb_intp>* expr_dependencies = new std::vector<cphvb_intp>();
            //expr_dependencies->insert(expr_dependencies->begin(),list->begin(),list->begin()+(i-1));        
            //return expr_dependencies;
        } else {
            return;
        }       
    }
    // if Name is greator then all usages, return the whole list.    
    return;
}

std::vector<cphvb_intp>* get_expr_dependencies(jit_base_dependency_table* bd_table, std::vector<cphvb_intp>* dep_list, jit_expr* expr) {
    printf("get_expr_dependencies()");
    if (expr->tag == array_val) {
        if (expr->op.array->base != NULL) {
            std::vector<cphvb_intp>* list = jita_base_usage_table_get_usage(bd_table,expr->op.array->base); 
            get_expr_base_dependencies(list,dep_list, expr->name);                    
        }
    } else if(expr->tag == bin_op) {
        get_expr_dependencies(bd_table,dep_list,expr->op.expression.left);
        get_expr_dependencies(bd_table,dep_list,expr->op.expression.right);
        
    } else if(expr->tag == un_op) {
        get_expr_dependencies(bd_table,dep_list,expr->op.expression.left);
    }
    return NULL;
}





cphvb_intp get_prior_element(vector<cphvb_intp>* base_dep, cphvb_intp elem) {
    if (base_dep == NULL)
        return -2;
    
    vector<cphvb_intp>::iterator it;
    cphvb_intp prior = -1;
    int i;
    
    for(i=0; i<base_dep->size();i++) {
        if(base_dep->at(i) < elem) {
            prior = base_dep->at(i);                                    
        } else {
            break;
        }            
    }
    return prior;    
}

cphvb_intp update_expr_dependencies(jit_analyse_state* s, jit_dependency_graph* graph, jit_dependency_graph_node* nt_entry_node, jit_expr* expr) {

    logDebug("update_expr_dependencies - ntname %d, expr name%d\n",nt_entry_node->name_table_entry->expr->name, expr->name);
    cphvb_array* a;
    if (!is_constant(expr)) {                                            
        jit_name_entry* entr = jita_nametable_lookup(s->nametable,expr->name);
        
        logDebug(" ! %p %p\n",entr->arrayp->base, entr->arrayp->data);
        if ( cphvb_base_array(entr->arrayp)->data != NULL) {
            a = cphvb_base_array(entr->arrayp);
            vector<cphvb_intp>* base_dep = jita_base_usage_table_get_usage(s->base_usage_table,a);
            cphvb_intp dep_on_name = get_prior_element(base_dep,nt_entry_node->name_table_entry->expr->name);
            
            logDebug("dependenylist for %p",a);
            //jit_pprint_base_dependency_list(base_dep);
            if (dep_on_name != -1) {
                logDebug("updating dependency! dep_on_name: %d\n",dep_on_name);
                jit_dependency_graph_node* tnode = jita_depgraph_get_node(graph,dep_on_name);

                if (dep_on_name != nt_entry_node->name_table_entry->expr->name) {

                    // dep_graph_nodes
                    jita_depgraph_node_add_don(nt_entry_node,tnode->name_table_entry);                
                    jita_depgraph_node_add_dto(tnode,nt_entry_node->name_table_entry);

                    // nametable entries dependency
                    if (nt_entry_node->name_table_entry->tdon == NULL) {                        
                        nt_entry_node->name_table_entry->tdon = new set<cphvb_intp>();                                                
                    }
                    nt_entry_node->name_table_entry->tdon->insert(tnode->name_table_entry->expr->name);
                    if (tnode->name_table_entry->tdto == NULL) {
                        tnode->name_table_entry->tdto = new set<cphvb_intp>();                        
                    }
                    tnode->name_table_entry->tdto->insert(nt_entry_node->name_table_entry->expr->name);
                }

            }            
        }
        
    }
    return 0;
}

// not tested !!!
cphvb_intp reduce_identity(jit_analyse_state* s,jit_expr* expr) {
    if (expr->tag == un_op) {
        if (expr->op.expression.opcode == CPHVB_IDENTITY) {            
            // When the AST is build matching views are included into the tree,
            // adding the identity instruction when view pointers are not the same.

            jit_name_entry* entr = jita_nametable_lookup(s->nametable,expr->name);

            // if and identity has a array as input it as assignment or a type conversion
            if (expr->op.expression.left->tag == array_val) {
                if (expr->op.expression.left->op.array->base != NULL && entr->arrayp->base != NULL) {
                    // base to base assignment, No reduction.                
                    return 1;
                                        
                } else if (entr->arrayp->type != expr->op.expression.left->op.array->type) {
                    // type conversion. No reduction
                    return 2;
                }
                

            // if Identity has and expression as input it is originally writting a temp array into the target,
            // and can thus be reduced, by letting the target of the Identity, become the target of the subexpression.
            } else {
                
                jit_name_entry* subEntr = jita_nametable_lookup(s->nametable,expr->op.expression.left->name);
                    
                // if arithmetic operation assigned to output.             
                // set input array to target array!
                subEntr->arrayp = entr->arrayp;
                return 0;
            }            
        }
        return 3;
    }
    return 4;
}

// given a list of entries to execute, build a complete list of all dependency chains.
// works!
void depend_on_travers(jit_analyse_state* s, jit_dependency_graph* graph, set<cphvb_intp>* execute_list, vector<jit_name_entry*>* dons) {    
    int i;
    cphvb_intp tname;
    vector<jit_name_entry*>* ds;
    
    for(i=0;i < dons->size();i++) {        
        if (dons->at(i)->expr->tag != array_val) {
            tname = dons->at(i)->expr->name;
            // if the name is not in the list, add it to the execute list.        
            execute_list->insert(tname);
                    
            ds = jita_depgraph_get_don(graph,tname);
            if (ds != NULL ) {
                depend_on_travers(s,graph,execute_list,ds);
            }    
        }        
    }
}




void expr_pre_execute_list_brach2(jit_analyse_state* s, vector<jit_expr*>* executelist, set<cphvb_intp>* expr_dons ,jit_expr* parent_expr, jit_expr* expr) {    
}

void expr_pre_execute_list(jit_analyse_state* s, vector<jit_expr*>* executelist, jit_expr* expr);
void expr_pre_execute_list_brach(jit_analyse_state* s, vector<jit_expr*>* executelist,set<cphvb_intp>* expr_dons ,jit_expr* parent_expr, jit_expr* expr) {
    printf("name %ld, depth: %d\n",expr->name,expr->depth);        
    vector<jit_name_entry*>* dtos = jita_depgraph_get_dto(s->dep_graph, expr->name);
    printf("name %ld, dtos count: %d\n",expr->name,dtos->size());        
    if (is_array(expr)) {
        
    }
    
    if(expr->depth == 0) {
        printf(" child node %ld\n",parent_expr->name);
        // erase child dto's from expr.            
        for(int i=0;i<dtos->size();i++) {
            if (dtos->at(i)->expr->name == parent_expr->name) {
                expr_dons->erase(expr->name);
                printf("erase: %d \n",dtos->at(i)->expr->name);
            }
            
            
        }
        jit_pprint_set(expr_dons);
    } else {
        
        //printf(" ++ 2 %p\n",dtos);
        // do cuts in the left tree.            
        if (dtos->size() > 1 ) {
            printf(" ++ 21\n");
            // if multi dependencies, the branch must be computed separately

            // set status to executed, to treat the entry as a array_val in
            // code generation.
            expr->status = jit_expr_status_executed;

            // Investige subtree for dependencies.
            expr_pre_execute_list(s,executelist,expr);               
            executelist->push_back(expr);
            
        } else if (dtos->size() == 1) {
            printf("++22 -  \n",parent_expr->name);
            cphvb_intp dtos0name = dtos->at(0)->expr->name;
            printf("++22 -  %ld  %ld  %ld\n",parent_expr->name, expr->name, dtos0name);
            if (parent_expr->name == dtos0name) {
                //printf(" ++ 221\n");
                expr_dons->erase(dtos0name);
                expr_pre_execute_list(s,executelist,expr);
                
                 
            } else if (parent_expr->name < dtos0name) {
                //printf(" ++ 222\n");
                // if the child has a dependency to a expression declared after this
                // expression.
                expr->status = jit_expr_status_executed;                
                expr_pre_execute_list(s,executelist,expr);                    
                executelist->push_back(expr);
            }
        } else {
            printf("out of dtos thing\n");
        }
    }
    printf("execution-"); jit_pprint_list_of_expr(executelist);
}


void create_expr_deps(jit_analyse_state* s,jit_expr* expr) {
    logDebug("create_expr_deps()\n");
    if (!is_array(expr) && !is_constant(expr)) {
        jit_name_entry* entr = jita_nametable_lookup(s->nametable,expr->name);

        
        vector<jit_name_entry*>* dons = jita_depgraph_get_don(s->dep_graph,expr->name);
        if (entr->tdon == NULL) {
            entr->tdon = new set<cphvb_intp>();
        }
        for(int i=0;i<dons->size();i++) {

            entr->tdon->insert(dons->at(i)->expr->name);
        }        
        vector<jit_name_entry*>* dtos = jita_depgraph_get_dto(s->dep_graph,expr->name);
        if (entr->tdto == NULL) {
                entr->tdto = new set<cphvb_intp>();
            }
        for(int i=0;i<dtos->size();i++) {
            
            entr->tdto->insert(dtos->at(i)->expr->name);
        }        
    }    
}

void expr_preexelist_travers(jit_analyse_state* s, set<cphvb_intp>* exelist, jit_expr* expr) {
    if (is_array(expr)) {
        printf("---------%ld---------\n",expr->name);
    }else
    if (is_un_op(expr) || is_bin_op(expr)) {
        jit_name_entry* entr = jita_nametable_lookup(s->nametable,expr->name);
        printf("%ld tdto size:%d\n",expr->name,entr->tdto->size());
        if (entr->tdto->size() > 0) {
            //jit_pprint_set(entr->tdto);
            
            exelist->insert(expr->name);
        }
        
        expr_preexelist_travers(s, exelist, expr->op.expression.left);
    } else
    if(is_bin_op(expr)) {
        
        expr_preexelist_travers(s, exelist, expr->op.expression.right);
    }
    
}
void print_true_false(bool stm) {
    if(stm)
        printf("true");
    else
        printf("false");
}

void expr_dependecy_travers2(jit_analyse_state* s,jit_expr* expr,set<cphvb_intp>* execute_list) {
    logInfo("expr_dependecy_travers2(%p,%ld)\n",s,expr->name);
    if (is_array(expr)) {
         return;
    }
    jit_expr* echild;
    jit_name_entry* entr_child;
    jit_name_entry* entr = jita_nametable_lookup(s->nametable,expr->name);
    set<cphvb_intp>::iterator it;
    

    if (is_un_op(expr) || is_bin_op(expr)) {
        // for the current expr.
        // for each don not with a dton in its child expr.
        for(it = entr->tdon->begin();it!= entr->tdon->end();it++) {
                      
            //printf("on EXEADD %ld: %ld\n",expr->name,*it);
            execute_list->insert(*it);
            logDebug("add %ld: %ld  %d_\n",expr->name,*it,expr->depth);
        }

        // child
        echild = expr->op.expression.left;                
        entr_child = jita_nametable_lookup(s->nametable,echild->name);
        if (!is_array(echild)) {

             
            //print_true_false(entr->tdon->find(echild->name) == entr->tdon->end()); printf(" "); printf("size = %d ", entr->tdon->size()); 

            if (entr->tdon->find(echild->name) == entr->tdon->end()) {                
                // if the child writes to a base array and it is not on the dependOn of its parent.
                // A[] = expr
                if(cphvb_base_array(entr_child->arrayp)->data != NULL && echild->depth > 0) {

                    // if the expression is IDENTIY, it will at this point only be for assignment of temp array. A = A + B instead of A += B. 
                    if(echild->op.expression.opcode != CPHVB_IDENTITY) {                                        
                        execute_list->insert(echild->name); 
                        //printf("add %ld: %ld %d.\n",expr->name, echild->name, echild->depth);
                    }
                }
            }
                            

           //printf("%ld",echild->name); jit_pprint_set(entr_child->tdto); jit_pprint_set(entr_child->tdon);

            if (entr_child->tdto->size() > 0) {
                execute_list->insert(echild->name);
                logDebug("add %ld: %ld ,\n",expr->name,echild->name);
                //printf("EXEADD %ld:\n",echild->name);
                //printf("EXESET %ld:\n",echild->name);
            }

            //~ 
            //~ entr_child->tdto->begin();
            //~ for(it = entr_child->tdto->begin();it!= entr_child->tdto->end();it++) {            
                //~ printf("to %ld: %ld\n",echild->name,*it);
            //~ }
                                
            expr_dependecy_travers2(s,echild,execute_list);
        }                  
    }

    if(is_bin_op(expr)) {

        echild = expr->op.expression.right;
        entr_child = jita_nametable_lookup(s->nametable,echild->name);    
        if (!is_array(echild)) {             
            if (entr->tdon->find(echild->name) == entr->tdon->end()) {
                if(cphvb_base_array(entr_child->arrayp)->data != NULL && echild->depth > 0) {
                    //printf("OOOOOOOOOOOOOOOOO echild->name %ld not in %ld \n",echild->name,expr->name);
                    execute_list->insert(echild->name);
                    logDebug("add %ld: %ld %d'\n",expr->name,echild->name,echild->depth);
                }
            }
                            

            //printf("%ld",echild->name); jit_pprint_set(entr_child->tdto); jit_pprint_set(entr_child->tdon);

            if (entr_child->tdto->size() > 0) {
                execute_list->insert(echild->name);
                logDebug("add %ld: %ld *\n",expr->name,echild->name);
                //printf("EXEADD %ld:\n",echild->name);
                //printf("EXESET %ld:\n",echild->name);
            }

            //~ 
            //~ entr_child->tdto->begin();
            //~ for(it = entr_child->tdto->begin();it!= entr_child->tdto->end();it++) {            
                //~ printf("to %ld: %ld\n",echild->name,*it);
            //~ }
                                
            expr_dependecy_travers2(s,echild,execute_list);
        }          
    }
    
}

cphvb_instruction* copy_instruction(cphvb_instruction* instr) {
    logDebug("copy_instruction()\n");
    cphvb_instruction* ninstr = (cphvb_instruction*)malloc(sizeof(cphvb_instruction));
    
    ninstr->status = instr->status;
    logDebug("status: %ld\n",ninstr->status);
    
    ninstr->opcode = instr->opcode;
    logDebug("opcode: %ld\n",ninstr->opcode);
    
    *ninstr->operand = *instr->operand;
    
    ninstr->constant = instr->constant;
    logDebug("constant: %ld\n",ninstr->constant);
    
    ninstr->userfunc = instr->userfunc;

    return ninstr;
}

/// depth of expr is 1
cphvb_intp execute_instruction_simple(cphvb_instruction* instr) {
    logDebug("execute_instruction_simple()\n");
    // allocate memory for result if needed    
    if (cphvb_base_array(instr->operand[0])->data == NULL) {
        cphvb_data_malloc(instr->operand[0]);
    }
        
    
    // call simple with the expr->arrayp,expr->left->arrayp,expr->right->arrayp
    if (cphvb_compute_apply(instr) == CPHVB_SUCCESS) {
        //printf("compute_apply() == CPHVB_SUCCESS\n");
        return 0;
    }
            
    return 1;
}





/// depth of expr is 2, and top is IDENTITY
cphvb_instruction* convert_identity_op(jit_analyse_state* s,jit_name_entry* entr) {
    printf("convert_identity_op()\n");
    if (entr->expr->op.expression.opcode != CPHVB_IDENTITY) {
        printf("convert_identity == input not identity!\n");
        return NULL;
    }
    printf("1\n");
    cphvb_array* target_array = entr->arrayp;
    printf("2\n");
    cphvb_instruction* instr = jita_nametable_lookup(s->nametable,entr->expr->op.expression.left->name)->instr;

    cphvb_instruction* newInstr = copy_instruction(instr);
    printf("3 %p\n",newInstr->operand[0]);
    newInstr->operand[0] = target_array;
    printf("1\n");
    return newInstr;    
}



cphvb_intp execute_expression_multi(jit_analyse_state* s, jit_name_entry* entr) {
    bool verbose = false;
    bool compile_all = false;
    if(is_constant(entr->expr)) {
        printf("WTF!! a constant in preexecutelist!\n");
        return 1;
    }

    // arrays cannot be executed
    if(is_array(entr->expr)) {
        if (verbose)
            printf(" array. Skipped\n");
        return 0;
    }        

    
    // single instruction. Execute with simple!
    if(entr->expr->depth == 1 && !compile_all) {
        if (verbose)
            printf(" single depth expression.\n");
        
        // execute with simple
        cphvb_intp res = execute_instruction_simple(entr->instr);
        if (res) {
            entr->is_executed = true;
        }                
        return res;
    }
    
    //~ if(entr->expr->depth == 2 && entr->expr->op.expression.opcode == CPHVB_IDENTITY) {
        //~ printf(" double depth Identity.\n");
        //~ printf("");
        //~ cphvb_instruction* n = convert_identity_op(s,entr);
        //~ printf("newInstruction %p\n",n->operand[0]);
        //~ execute_instruction_simple(n);
        //~ return;
        //~ 
    //~ }

    
    
    jitcg_state* cgs = new jitcg_state();
    cgs->sync_expr_name = entr->expr->name;

    

    // todo. move into function in jit_compile
    //jitc_compile_state(cgs,s);

    int hash = abs((int) string_hasher(cgs->source_math));
    char buff[30];
    sprintf(buff,"kernel_func_%d",hash);
    cgs->kernel_name = buff;

    //printf("\\\\\\\\\\\\ %s\n",cgs->kernel_name.c_str());
    jitcg_create_kernel_code(cgs,s, entr->expr);
  
    jit_comp_kernel* kernel = jitc_compile_computefunction(cgs->kernel_name, cgs->source_function);
    cgs->kernel = kernel;


    //printf("output array %p\n",cphvb_base_array(entr->arrayp)->data);
    cphvb_intp res = cphvb_data_malloc( cphvb_base_array(entr->arrayp));
    //printf("output array %p\n",cphvb_base_array(entr->arrayp)->data);

     
    //if (kernel == NULL || false) {
    if (false) {        
        printf("kernel == null\n");        
    } else {
        printf("executing by kernel\n");
        //printf("kernel function: %p\n",kernel->function);
        //printf("kernel memaddr: %p\n",kernel->memaddr);
        //printf("kernel key: %ld\n", kernel->key);

        
        //printf("output: %p\n",cphvb_base_array(entr->arrayp)->data);
        //printf("math: %s\n",cgs->source_math.c_str());
        //printf("A inp %p\n",cgs->array_inputs);
        //printf("a len %d\n",cgs->array_inputs_len);
        //printf("con %p\n",cgs->constant_inputs);


        //printf("\n\n%s\n\n",cgs->source_function.c_str());

        printf("running kernel\n");
        
        //kernel_func_001(cphvb_base_array(entr->arrayp), cgs->array_inputs, (cphvb_index)cgs->array_inputs_len ,cgs->constant_inputs, 0, 0);
        kernel->function(cphvb_base_array(entr->arrayp), cgs->array_inputs, (cphvb_index)cgs->array_inputs_len ,cgs->constant_inputs, 0, 0);

        printf("exedone\n");
        //kernel->function(cphvb_base_array(output_array), cgs->array_inputs, cgs->array_inputs_len ,cgs->constant_inputs, cgs->constant_inputs_len, 0);
    }
    
                
    

    
    
    //jit_pprint_cg_state(cgs);

    

    if (verbose)
        printf(" expression depth: %ld\n", entr->expr->depth);
    return 0;  
    
}


/**
 * count the number of operations (binary / unary)
 * ignores executed expression (and following subexpression)
 * ignores identity
 */
cphvb_intp expr_opcount(jit_analyse_state* s, jit_expr* expr) {
    cphvb_intp opcount = 0;
    if(is_bin_op(expr)) {
        opcount++;
        if (!jita_nametable_lookup(s->nametable, expr->op.expression.left->name)->is_executed) {
            opcount += expr_opcount(s,expr->op.expression.left);
        }
        if (!jita_nametable_lookup(s->nametable, expr->op.expression.right->name)->is_executed) {
            opcount += expr_opcount(s,expr->op.expression.right);
        }
        
    } else if (is_un_op(expr)) {
        opcount++;
        if (! (jita_nametable_lookup(s->nametable, expr->op.expression.left->name)->is_executed)) {
            opcount += expr_opcount(s,expr->op.expression.left);
        }
    }    
    return opcount;
}

cphvb_intp array_elements_accessed(cphvb_array* a) {
    cphvb_intp count = 1;
    printf("a->dim: %ld\n",a->ndim);
    for (int i=0; i<a->ndim;i++) {
        printf("%f %ld %ld\n",( (float)a->shape[i] / (float)a->stride[i] ),a->shape[i],a->stride[i]);
        count *= cphvb_intp((float)a->shape[i] / a->stride[i]);
    }
    printf("count:%ld",count);
    return count;
}

/**
 * Calculates a number to compare expression complexity.
 * num_of_operations * array size. 
 */
cphvb_intp expr_computational_complexity(jit_analyse_state* s, jit_expr* expr) {
    // number of operations in the expression.
    
    cphvb_intp opcount = expr_opcount(s,expr);
    jit_name_entry* entr = jita_nametable_lookup(s->nametable,expr->name);    
    // if the target array for the expression, does not have a view (is a base),
    // the number of

    cphvb_intp accessed_elements = cphvb_nelements(entr->arrayp->ndim,entr->arrayp->shape);
    //printf("acc.elems: %ld \n",accessed_elements);
    //printf("opcount  : %ld \n",opcount);

    
    return accessed_elements * opcount;    
}



cphvb_intp execute_expression(jit_analyse_state* s, set<cphvb_intp>* preexecutelist, cphvb_intp executename) {
    // for each element in preexecutelist, execute the element if is not already executed (checked with is_executed(expr) ).
    bool verbose = false;    
    jit_name_entry* entr;
    set<cphvb_intp>::iterator it;

    if (verbose)
        printf("\nBegin pre- expression execution\n===============================\n");
    for(it = preexecutelist->begin(); it != preexecutelist->end();it++) {

        if (verbose)
            printf("== %ld ", *it);
        entr = jita_nametable_lookup(s->nametable,(*it));

        execute_expression_multi(s,entr);              
    } 
             
    // after all prerequired elements have been executed, execute the main expression
    
    entr = jita_nametable_lookup(s->nametable,executename);
    if (verbose)
        printf("\nBegin main expression execution\n===============================\nexecuting: %ld\n",executename);
    

    // get the hash of the expr
    // check cache for existing kernel


    // if not, create kernel.
    // 1) create code
    // 2) compile into kernel.
    //printf("Complexity: %ld\n", expr_computational_complexity(s,entr->expr));
    return execute_expression_multi(s,entr);
    
    
    /*

    */
    
}


/// travers an expr and removes dependencies if these are represented
/// children removes from parrent.
void expr_dependecy_travers(jit_analyse_state* s,jit_expr* expr) {
    logDebug("--expr_dependecy_travers(%p,%ld)\n",s,expr->name);
    jit_expr* echild;
    jit_name_entry* entr_child;
    jit_name_entry* entr = jita_nametable_lookup(s->nametable,expr->name);
    logDebug("ent tdon %p . tdto %p\n",entr->tdon,entr->tdto);

    // if parent is and Identity operation, dependencies should be extended
    // to 

    
    if (is_un_op(expr) || is_bin_op(expr)) {

        if(expr->op.expression.opcode == CPHVB_IDENTITY) {
            //printf("IIIIIIIIIIIIII    %ld\n",expr->name);

            // child can be and temp array or a base array.
            

            if(is_bin_op(expr->op.expression.left)) {
                entr->tdon->erase(expr->op.expression.left->op.expression.left->name);
                entr->tdon->erase(expr->op.expression.left->op.expression.right->name);
                
                entr_child = jita_nametable_lookup(s->nametable,expr->op.expression.left->op.expression.left->name);
                entr_child->tdto->erase(expr->name);
                entr_child = jita_nametable_lookup(s->nametable,expr->op.expression.left->op.expression.right->name);
                entr_child->tdto->erase(expr->name);
            }
            
            // remove dto of current node in child node.
            //printf("Lc childnode's tdto %p\n",entr_child->tdto);
            //entr_child->tdto->erase(expr->name);
        
        }
    
        echild = expr->op.expression.left;        
        entr_child = jita_nametable_lookup(s->nametable,echild->name);
        logDebug("Lc %ld \n",echild->name);
        
        
        
        if (is_array(echild)) {
            logDebug("Lc A Erase %ld from %ld \n",echild->name,entr->expr->name);

            entr->tdon->erase(echild->name);

            //entr_child = jita_nametable_lookup(s->nametable,echild->name);
            entr_child->tdto->erase(expr->name);
                    
            //if (entr_child->tdto->size() > 1)
            
        } else         
        if (is_un_op(echild) || is_bin_op(echild)) {
            // remove depend on from current node.



            // the child is not in the DON list of its parrent.            
            if (entr->tdon->find(echild->name) == entr->tdon->end()) {
                if(cphvb_base_array(entr_child->arrayp)->data != NULL) {
                    // the target of this the expression, are the same as the input of the parrent,
                    // and as this is a base array, the input to the parrent must be a base input aswell.
                    // As there is no direct dependence, another assignment must be done to the same base array
                    // at a later point, which does not match the shape of the expression.

                    // what to do.
                    // we must execute this child expression, before the dto of it.
                    // mark echild as "execute"
                    // add to "preexecute list"
                    //  ** traversel to this element from the expression root, is done by recursively building pre-execution
                    //  ** on execute elements.

                    logDebug("Lc child %ld not in parrent DON\n",echild->name);
                    
        
                } else {
                    logDebug("Lc $$$\n");
                } // else a view
                
                
            } else {
                logDebug("Lc %ld from %ld \n",echild->name,entr->expr->name);
                entr->tdon->erase(echild->name);
                
                // remove dto of current node in child node.
                logDebug("Lc childnode's tdto %p\n",entr_child->tdto);
                entr_child->tdto->erase(expr->name);
            }    
                        
            expr_dependecy_travers(s,echild);
        }            
    }

    if(is_bin_op(expr)) {
        echild = expr->op.expression.right;
        logDebug("Rc %ld \n",echild->name);
        entr_child = jita_nametable_lookup(s->nametable,echild->name);            
        if (is_array(echild)) {
            logDebug("erase r %ld from %ld *\n",echild->name,entr->expr->name);
            entr->tdon->erase(echild->name);
            
            entr_child->tdto->erase(expr->name);
        } else


        if (is_un_op(echild) || is_bin_op(echild)) {
            // remove depend on from current node.
            

            // the child is not in the DON list of its parrent.            
            if (entr->tdon->find(echild->name) == entr->tdon->end()) {
                if(cphvb_base_array(entr_child->arrayp)->data != NULL) {
                    // the target of this the expression, are the same as the input of the parrent,
                    // and as this is a base array, the input to the parrent must be a base input aswell.
                    // As there is no direct dependence, another assignment must be done to the same base array
                    // at a later point, which does not match the shape of the expression.

                    // what to do.
                    // we must execute this child expression, before the dto of it.
                    // mark echild as "execute"
                    // add to "preexecute list"
                    //  ** traversel to this element from the expression root, is done by recursively building pre-execution
                    //  ** on execute elements.

                    logDebug("Rc child %ld not in parrent DON\n",echild->name);
        
                } // else a view
                
            } else {
                logDebug("Rc %ld from %ld \n",echild->name,entr->expr->name);
                entr->tdon->erase(echild->name);
                
                // remove dto of current node in child node.
                logDebug("Rc childnode's tdto %p\n",entr_child->tdto);
                entr_child->tdto->erase(expr->name);
            }    
                        
            expr_dependecy_travers(s,echild);
        }               
        //~ if (is_un_op(echild) || is_bin_op(echild)) {
            //~ // remove depend on from current node.
            //~ printf("erase %ld from %ld \n",echild->name,entr->expr->name);
            //~ entr->tdon->erase(echild->name);
//~ 
            //~ printf("get childnode %ld 's tdto and remove %ld from it\n",echild->name,expr->name);
            //~ // remove dto of current node in child node.
            //~ entr_child = jita_nametable_lookup(s->nametable,echild->name);
            //~ printf("childnode's tdto %p\n",entr_child->tdto);
            //~ entr_child->tdto->erase(expr->name);
                        //~ 
            //~ expr_dependecy_travers(s,echild);
        //~ }  
    }
    
}

void expr_travers(jit_analyse_state* s, vector<jit_expr*>* executelist, jit_expr* expr) {

    vector<jit_name_entry*>* dons;
    if (is_un_op(expr) || is_bin_op(expr)) {
        if (is_constant(expr->op.expression.left) || is_array(expr->op.expression.left)) {
            printf("e: %ld lc: VAL\n",expr->name);
            return;
        }   
        printf("e: %ld lc: %ld \n",expr->name,expr->op.expression.left->name);

        
        dons = jita_depgraph_get_don(s->dep_graph,expr->op.expression.left->name);
        printf("   %ld lc DONs [",expr->op.expression.left->name);
        for(int i=0;i<dons->size();i++) {
            printf("%ld, ",dons->at(i)->expr->name);
        }
        printf("]\n");
        dons = jita_depgraph_get_dto(s->dep_graph,expr->op.expression.left->name);
        printf("   %ld lc DTOs [",expr->op.expression.left->name);
        for(int i=0;i<dons->size();i++) {
            printf("%ld, ",dons->at(i)->expr->name);
        }
        printf("]\n");
        
        
        expr_travers(s, executelist, expr->op.expression.left); 
        //expr_pre_execute_list_brach(s, executelist, expr_dons , expr, expr->op.expression.left);        
    }
        
    if(is_bin_op(expr)) {
        if (is_constant(expr->op.expression.right) || is_array(expr->op.expression.right)) {
            printf("e: %ld rc: VAL\n",expr->name);
            return;
        }         
        printf("e: %ld rc: %ld \n",expr->name,expr->op.expression.right->name);
        dons = jita_depgraph_get_don(s->dep_graph,expr->op.expression.right->name);
        printf("   %ld rc DONs [");
        for(int i=0;i<dons->size();i++) {
            printf("%ld, ",dons->at(i)->expr->name);
        }
        printf("]\n");
        dons = jita_depgraph_get_dto(s->dep_graph,expr->op.expression.right->name);
        printf("   %ld rc DTOs [",expr->op.expression.right->name);
        for(int i=0;i<dons->size();i++) {
            printf("%ld, ",dons->at(i)->expr->name);
        }
        printf("]\n");
        expr_travers(s, executelist, expr->op.expression.right) ;
        //expr_pre_execute_list_brach(s, executelist, expr_dons , expr, expr->op.expression.right);        
    }   
}


// in testing
void expr_pre_execute_list(jit_analyse_state* s, vector<jit_expr*>* executelist, jit_expr* expr) {
    if (is_array(expr) || is_constant(expr)) {
        return;
    }
    set<cphvb_intp>* expr_dons = new set<cphvb_intp>();    
    //jit_name_entry* entr = jita_nametable_lookup(s->nametable,expr->name);        
    vector<jit_name_entry*>* dons = jita_depgraph_get_don(s->dep_graph,expr->name);
    
    for(int i=0;i<dons->size();i++) {
        expr_dons->insert(dons->at(i)->expr->name);
        logDebug("expr_dons creation:%ld, %ld\n", expr->name,dons->at(i)->expr->name);
    }
    
            
    if (is_un_op(expr) || is_bin_op(expr)) {
        expr_dons->erase(expr->op.expression.left->name);
        expr_pre_execute_list(s, executelist, expr->op.expression.left); 
        //expr_pre_execute_list_brach(s, executelist, expr_dons , expr, expr->op.expression.left);        
    }
    
    if(is_bin_op(expr)) {
        expr_dons->erase(expr->op.expression.right->name);
        expr_pre_execute_list(s, executelist, expr->op.expression.right) ;
        //expr_pre_execute_list_brach(s, executelist, expr_dons , expr, expr->op.expression.right);        
    }

    logDebug("execution1-"); jit_pprint_list_of_expr(executelist);
    if(expr_dons->size() > 0) {
        set<cphvb_intp>::iterator it; 
        for(it=expr_dons->begin();it != expr_dons->end(); it++) {
            jit_name_entry* ent = jita_nametable_lookup(s->nametable,*it);
            executelist->insert(executelist->begin(),ent->expr);
        }       
    }
    logDebug("execution2-"); jit_pprint_list_of_expr(executelist);
     

           

            
/*
    # do cuts in left tree or remove child/parent relations
    if el.dtos.size() > 1:
        el.status = executed
        expr_execute_list(, , ,el);
        add_to_executelist(el);
    elif el.dtos.size() == 1: 
        if (expr->name == el.dto[0]):
            local_dons.remove(el.dto[0])
        elif el.dto[0] > expr->name:
            el.status = executed        
            expr_execute_list(, , ,el);
            add_to_executelist(el);
*/
       
    //~ 
    //~ if (e->dtos.size() > 1) {
        //~ e->status = execute;
        
        //~ add_to_execute(e);

        //~ 
    //~ } else if (e->dtos.size() == 1) {            
        //~ if(e->parent->name < e->dto[0]) {
            //~ e->status = execute;
            
            //~ add_to_execute(e);
            
        //~ }
    //~ }
//~ 
    //~ if e->dons.size() > 0 {
        //~ e.preExecute.add(dons)
    //~ }
//~ 
    //~ 
    //~ if(e->parent == NULL) {
        //~ // add root to execute
        //~ add_to_execute(e);
        //~ e->status = execute;
    //~ }   
}




// this functions takes an array and from this determines which expressions must be
// executed and in what order. a list of expressions are generated based on the
// analyse_state tables. This will contain a copy of the existing structure, but point
// to the existing data.
set<cphvb_intp>* generate_execution_list(jit_analyse_state* s, cphvb_array* sync_array) {
    logDebug("===== Generate_execution_list()\n");
    // loop through the sync_array's expression structure.
    // top down, left to right.
    
    // if a array with base array is used as input, determin its dependency expression.
    // store at DON and update its DTO. Create copy of DON info (small lookuptable with array, DON+DTO
    // An array is dependent on another, if
    // 1) it has a base array.
    // 2) the base arrays usage_table has one or more entries, which are smaller then the name of the expression
    //    forming the result of the array.
    // 3) the array is a base_array with a usage_table with one or more entries.
    
    // reduce Identity expression in expressions related to the sync_array.
    
    // if dependencies can are resolved by child expressions, it is not an dependency for the expression
    // if an expression DTO has more then one entry, the 'other' should be computes into a temp array,
    // before the main expression can be made into a kernel, or simple-computed.


    // ====================================================
    jit_name_entry* entr;    
    
    vector<cphvb_intp>* base_dep;    
    
    cphvb_intp dep_on_name;
    jit_dependency_graph* dep_graph = new jit_dependency_graph();
    
    jit_name_table::iterator it;
    jit_dependency_graph_node* dg_node;
    jit_dependency_graph_node* tnode;
    cphvb_array* basea;
    cphvb_intp name;


    //~ for(it=s->nametable->begin(); it != s->nametable->end(); it++) {
        //~ entr = (*it);                    
        //~ name = entr->expr->name;
        //~ jita_depgraph_add_node(dep_graph,entr);
        //~ printf("name: %d\n",name);
    //~ }
    //~ printf("created initial dep graph %d\n", dep_graph->size());
    
    // Optimization: This will go trough the complete name table every time, it could
    // be a good optimization to do incremental updates of the dependencies, thus introduce
    // and offset to the start on the nametable.
    logInfo("nametable size: %ld\n",s->nametable->size());
    logInfo("===== Build dependencies for Nametable\n");

    for(it=s->nametable->begin(); it != s->nametable->end(); it++) {
        logDebug("NT: %ld\n",(*it)->expr->name);
        // iterator points to name_entry.
        entr = (*it);                    
        name = entr->expr->name;
        dg_node = jita_depgraph_add_node(dep_graph,entr);            
        if (dg_node == NULL) {
            logError("failed to add node to dep_graph");
            ; // handle 
        }
                
        if(is_un_op(entr->expr) || is_bin_op(entr->expr)) {  // left expr dependency
            logDebug("un ");
            update_expr_dependencies(s,dep_graph,dg_node,entr->expr->op.expression.left);                        
        }
        
        if(is_bin_op(entr->expr)) {  // right expr dependency
            logDebug("bin ");
            update_expr_dependencies(s,dep_graph,dg_node,entr->expr->op.expression.right);                        
        }

        if (entr->tdon == NULL) {                        
            entr->tdon = new set<cphvb_intp>();                                                
        }       
        if (entr->tdto == NULL) {
            entr->tdto = new set<cphvb_intp>();                        
        }

        basea = NULL;
        //logDebug("%d] %p - %p\n",name,entr->arrayp->base,entr->arrayp->data);
        if ( !(entr->arrayp->base == NULL && entr->arrayp->data == NULL)) {
            
            basea = cphvb_base_array(entr->arrayp);
            //logDebug("basea set! %p \n",basea);
        }    
        
        if (basea != NULL) {
            // The entr affects the basearray by changeing it.
            // This has been recorded in the 'instruction handler', who build
            // the initial analyse_state.

            // this also means that this is dependent on previous changes.
            // It is unknown how this entry, (with the view and expression), changes
            // the basea. It could be writing a single value, a row or everything is changed.
            // This we do not know. (Might be able to figure this out later).
            
            // Add lookup usage_list of the basea.
            
            base_dep = jita_base_usage_table_get_usage(s->base_usage_table,basea);
            
            dep_on_name = get_prior_element(base_dep,name);            
            logDebug("base_dep.size() %d. dep_on: %d name:%d\n",base_dep->size(),dep_on_name,name);
            //jit_pprint_dependency_graph(dep_graph);
            if (dep_on_name > -1) {
                // the entry depends on dep_on_name.
                // add dep_on_name to DependON list for the entry.
                tnode = jita_depgraph_get_node(dep_graph,dep_on_name);
                
                // if the arrayp of dg_node and the temp node are the same, the expression has two dependencies to the
                // same entry. We skip the last one, since it is redundant.
                if (tnode != dg_node && tnode->name_table_entry->arrayp != dg_node->name_table_entry->arrayp) {
                    
                    jita_depgraph_node_add_don(dg_node,tnode->name_table_entry);                                        
                    jita_depgraph_node_add_dto(tnode,dg_node->name_table_entry);


                    
                    if (entr->tdon == NULL) {                        
                        entr->tdon = new set<cphvb_intp>();                                                
                    }
                    entr->tdon->insert(tnode->name_table_entry->expr->name);
                    if (entr->tdto == NULL) {
                        entr->tdto = new set<cphvb_intp>();                        
                    }
                    tnode->name_table_entry->tdto->insert(entr->expr->name);
                    
                }

                
            }                            
        }
        // printf("nametable name: %ld\n",name);
    }
    logInfo("===== Build dependencies DONE\n");
    
    jit_name_entry* sync_entr = jita_nametable_lookup_a(s,sync_array);
    if (sync_entr == NULL) {
        if (cphvb_base_array(sync_array)->data != NULL ) {
            logDebug("no result int nametable lookup a, and sync_array is a base array.\n");
            logDebug("setting sync_entry as the last use of sync_array's base\n");
            sync_entr = jita_nametable_lookup(s->nametable, jita_base_usage_table_get_usage(s->base_usage_table,sync_array)->back());
        }
    }


    
    
    //jit_pprint_dependency_graph(dep_graph);
    //jit_pprint_nametable(s->nametable);
    s->dep_graph = dep_graph;    
    set<cphvb_intp>* exelist = new set<cphvb_intp>();
    //create_expr_deps(s,entr->expr);
    //jit_pprint_set(exelist);

    //jit_pprint_nametable_dependencies(s->nametable);
    //printf("\n");
    //printf("%p\n",sync_entr);

    // travers and remove child/parent relations in dependencie graph.
    expr_dependecy_travers(s,sync_entr->expr); printf("\n\n");
    //jit_pprint_nametable_dependencies(s->nametable);

    // travers expr and create execution. Non recursive!! as of now. (can not handle dependencies in expression used in indirectly dependent expression.)
    expr_dependecy_travers2(s,sync_entr->expr,exelist);


    // create vector of jit_expr to return
    
    //printf("-----");jit_pprint_set(exelist);
    
    return exelist;
    
    
    








    

    
    /*
    //expr_preexelist_travers(s,exelist,entr->expr);

    jit_pprint_set(exelist);
    printf("\n\n");
    
    printf("\n\n");
    vector<jit_name_entry*>* dons1;
    dons1 = jita_depgraph_get_don(s->dep_graph,entr->expr->name);
    printf("   %ld lc DONs [",entr->expr->name);
    for(int i=0;i<dons1->size();i++) {
        printf("%ld, ",dons1->at(i)->expr->name);
    }
    printf("]\n");
    dons1 = jita_depgraph_get_dto(s->dep_graph,entr->expr->name);
    printf("   %ld lc DTOs [",entr->expr->name);
    for(int i=0;i<dons1->size();i++) {
        printf("%ld, ",dons1->at(i)->expr->name);
    }
    printf("]\n");
    */    
    
    /*
    //vector<jit_expr*>* executelist = new vector<jit_expr*>();
    //expr_travers(s,executelist,entr->expr);
    //printf("\n\n");
    
    if (sync_array->base == NULL && sync_array->data == NULL) {
        
        printf("temp array SYNC\n");
        // 1) follow the entr and build its dependencyes
        // 2) **do for all, and build a complete dependencygraph for all assignments.
        //    This could lead to a bit more work, but at it should be asumed that
        //    the code provided is needed, the end result should be related to all the
        //    parts.
        //    For later instructions lists after sync's, the right now not needed parts
        //    will be needed (again asumed that the code include only needed code and computations).
        
        // have all dependencies determined for the nametable entries.
    
        jit_name_entry* entr = jita_nametable_lookup_a(s,sync_array);

        // determin if there is dependencies in the expr.
        //~ vector<jit_expr*>* executelist = new vector<jit_expr*>();
        
        
        printf("pre-exelist size= %d\n",exelist->size());
        //printf("execution-"); jit_pprint_list_of_expr(executelist);
        
    } else {
        // dependencies of sync_array must all be computed.

        printf("basearray SYNC\n");
        int i;
        vector<cphvb_intp>* sync_array_usage = jita_base_usage_table_get_usage(s->base_usage_table, cphvb_base_array(sync_array));

        jit_pprint_base_dependency_list(sync_array_usage);
        if (sync_array_usage != NULL) {

            printf("creating execution list bsed on '%p' usage list\n",cphvb_base_array(sync_array));
            
            set<cphvb_intp>* execute_list = new set<cphvb_intp>();   
            for(i=0;i < sync_array_usage->size() ;i++) {
                printf("%d %d\n",i,sync_array_usage->size());
                vector<jit_name_entry*>* ds = jita_depgraph_get_dto(dep_graph,sync_array_usage->at(i));

                
                // dependent to
                if(ds->size() > 1) {
                    printf("\n");                    
                }
                ds = jita_depgraph_get_don(dep_graph,sync_array_usage->at(i));
                
                // dependent on
                if(ds != NULL) {
                    depend_on_travers(s,dep_graph,execute_list,ds);
                }
                printf("t\n");
                                            
                execute_list->insert(sync_array_usage->at(i));

                jit_dependency_graph_node* node = jita_depgraph_get_node(dep_graph,sync_array_usage->at(i));
                //~ node->dep_ons
                //~ 
                //~ execute_list->push_back();
            }
            
            printf("execute set! %d\n",execute_list->size());
            set<cphvb_intp>::iterator it;
            for(it=execute_list->begin();it!=execute_list->end();it++) {
                printf("%ld, ",*it);
            }
            printf("\n");

            
        }        
    }

    
    printf("done\n");
    */
}


/// old and not used!
vector<cphvb_intp>* jit_prepare_execute(jit_analyse_state* s, cphvb_array* sync_array) {    
    printf("s jit_prepare_execute()\n");
    if(sync_array->base == NULL) {
        // output is a basearray. compute all base_array_steps
        printf("array_base == NULL\n");        
        
        return jita_base_usage_table_get_usage(s->base_usage_table,sync_array);
    }

    // lookup the name for the output. Get the expression.
    cphvb_intp name = jita_ssamap_version_lookup(s->ssamap,sync_array,-1);                
    jit_name_entry* nt_entry = jita_nametable_lookup(s->nametable, name);                        
    if (nt_entry == NULL) {
        // no entry with this name.
        // nothing to execute.    
        return NULL;
    }
    
    printf("name: %ld\n",name);
    jit_pprint_ssamap(s->ssamap);
                            
    
    
    
    //std::vector<cphvb_intp>* expr_dependent_on = get_expr_dependencies(bd_table,nt_entry->expr);
    
    
    // travers the root expr. for base dependencies.
    
    
    
    // 
    printf("e jit_prepare_execute()\n");
}
        

cphvb_error jit_analyse_instruction_list(
        jit_ssa_map* jitssamap, 
        jit_name_table* jitnametable, 
        jit_base_dependency_table* bd_table,
        cphvb_intp instruction_count, cphvb_instruction* instruction_list) {

    cphvb_intp sync_name;
    cphvb_array* sync_array;    
    cphvb_array* op0;
    jit_expr* expr;
    cphvb_intp name;
    jit_name_entry* nt_entry;
    int count;
    cphvb_instruction* inst;
    std::vector<cphvb_intp>* execute_list ;
    jit_analyse_state* s;
    jit_base_dependency_table::iterator it;
    std::vector<cphvb_intp>::iterator itvec;

    set<cphvb_intp>* pre_execution_list;
    cphvb_intp execute_result;
    
    for(count=0; count < instruction_count; count++) { 
        inst = &instruction_list[count];                
        switch (inst->opcode) {                     // Dispatch instruction

            case CPHVB_NONE:                        // NOOP.
            case CPHVB_DISCARD:
                break;
            case CPHVB_SYNC:
                printf("SYNC");
                //cphvb_pprint_instr(inst);
                sync_array = inst->operand[0];
                //printf("name: %p\n",sync_array);


                s = jita_make_jit_analyser_state(jitnametable,jitssamap,jitbasedeptable);
                
                //jit_pprint_base_dependency_table(jitbasedeptable);
                //execute_list = jit_prepare_execute(s,inst->operand[0]);

                pre_execution_list = generate_execution_list(s,sync_array);
                sync_name = jita_ssamap_version_lookup(s->ssamap,sync_array,-1);
                execute_result = execute_expression(s,pre_execution_list,sync_name);

                inst->status = CPHVB_ERROR;
                if (execute_result == 0) {
                    inst->status = CPHVB_SUCCESS;
                }
                // build an expr to compute the value of the sync target
                //

                // determing which pars of the expr has dependencies, which
                // cannot be included into the expr, and ultimately into
                // the kernel.

                // make and execute_list, which is copies from the namelist of expr.
                // but these are modified before execution. will differ in the building of
                // these execution_trees.
                
                

                
                // get an complete list of dependencies to the expr.

                // execute the dependencies

                // 
                
                // create kernel
                // execute kernel

                
                
                
                /*
                name = jita_ssamap_version_lookup(jitssamap,sync_array,-1);
                jit_pprint_ssamap(jitssamap);
                printf(jit_pprint_nametable(jitnametable).c_str());
                
                printf("name: %ld\n",name);
                nt_entry = jita_nametable_lookup(jitnametable, name);
                
                jit_pprint_base_dependency_table(jitbasedeptable);                
                
                if (nt_entry != NULL) {
                    jit_pprint_expr(nt_entry->expr);
                }
                */
                break;
            case CPHVB_FREE:                        // Store data-pointer in malloc-cache
            case CPHVB_USERFUNC:
                break;
                
            default:
                //printf("tets");
                              
                jita_handle_arithmetic_instruction2(jitnametable,jitssamap,jitbasedeptable, &instruction_list[count]);
                break;
        }        
    }
    
    //printf("%s",jit_pprint_nametable(jitnametable).c_str());
    
}


cphvb_error cphvb_ve_jit_execute( cphvb_intp instruction_count, cphvb_instruction* instruction_list )
{
    bool output_testing = true;
    // Debug - print the execution list
    
    
    if (output_testing) {
        //cphvb_pprint_instr_list(instruction_list,instruction_count,"Testing!"); 
        //printf("Running Analyser\n");
        jit_analyse_instruction_list(jitssamap,jitnametable,jitbasedeptable,instruction_count,  instruction_list); 
                        
        //printf("Executing with simple\n\n\n\n");
        //execute_simple(instruction_count,instruction_list);
        return CPHVB_SUCCESS;
    }
    
    cphvb_intp count;
    cphvb_instruction* inst;
    
       
    for(count=0; count < instruction_count; count++)
    {   
        inst = &instruction_list[count];
        if (inst->status == CPHVB_SUCCESS) {        // SKIP instruction
            continue;
        }
             
        
        printf("++ %s\n",cphvb_opcode_text(inst->opcode));
        // is controll instruction
        if (inst->opcode == CPHVB_SYNC) {

            // execute             
            cphvb_intp name = jita_ssamap_version_lookup(jitssamap,inst->operand[0],-1);
            jit_name_entry* ne = jita_nametable_lookup(jitnametable,name);          
            
            if (CPHVB_SUCCESS != cphvb_data_malloc(ne->arrayp)) {
                printf("ERROR: Failed to allocate return array.");
                return CPHVB_ERROR;
            }                     
                                                                            
            test_computation(ne->arrayp,ne->expr,jitssamap,jitnametable);            
            
            stringstream pss; 
            printf("++++++++++++++++\nresult:\n");
            jitcg_print_cphvb_array( ne->arrayp,5, &pss);
            printf("%s\n\n",pss.str().c_str());
                        
            inst->status = CPHVB_SUCCESS;
            
        } else if (inst->opcode == CPHVB_FREE) {                    
            // add the array to the list of allocations to be free'd (only makes sense if they have been allocated in the first place)
            // inst->status = cphvb_mcache_free( inst );
        } else if (inst->opcode == CPHVB_DISCARD) {            
        
            
        // instruction is userfunction
        } else if (inst->opcode == CPHVB_USERFUNC){        
            
            if (inst->userfunc->id == reduce_impl_id) {
                inst->status = reduce_impl(inst->userfunc, NULL);
            } else if(inst->userfunc->id == random_impl_id) {
                inst->status = random_impl(inst->userfunc, NULL);
            } else if(inst->userfunc->id == matmul_impl_id) {
                inst->status = matmul_impl(inst->userfunc, NULL);
            } else {                            // Unsupported userfunc             
                inst->status = CPHVB_USERFUNC_NOT_SUPPORTED;
            }
        
        // is math operation          
        } else {                    
                                
        } 
        
        printf("setting prev_instr\n");       
        jit_prev_instruction = &instruction_list[count];  
                           
    }
    
    return CPHVB_SUCCESS;   
    //return execute_simple(instruction_count,instruction_list);    
}

cphvb_error cphvb_ve_jit_shutdown( void )
{
    // De-allocate the malloc-cache
    cphvb_mcache_clear();
    cphvb_mcache_delete();

    return CPHVB_SUCCESS;
}

cphvb_error cphvb_ve_jit_reg_func(char *fun, cphvb_intp *id) 
{
    if(strcmp("cphvb_reduce", fun) == 0)
    {
    	if (reduce_impl == NULL)
    	{
			cphvb_component_get_func(myself, fun, &reduce_impl);
			if (reduce_impl == NULL)
				return CPHVB_USERFUNC_NOT_SUPPORTED;

			reduce_impl_id = *id;
			return CPHVB_SUCCESS;			
        }
        else
        {
        	*id = reduce_impl_id;
        	return CPHVB_SUCCESS;
        }
    }
    else if(strcmp("cphvb_random", fun) == 0)
    {
    	if (random_impl == NULL)
    	{
			cphvb_component_get_func(myself, fun, &random_impl);
			if (random_impl == NULL)
				return CPHVB_USERFUNC_NOT_SUPPORTED;

			random_impl_id = *id;
			return CPHVB_SUCCESS;			
        }
        else
        {
        	*id = random_impl_id;
        	return CPHVB_SUCCESS;
        }
    }
    else if(strcmp("cphvb_matmul", fun) == 0)
    {
    	if (matmul_impl == NULL)
    	{
            cphvb_component_get_func(myself, fun, &matmul_impl);
            if (matmul_impl == NULL)
                return CPHVB_USERFUNC_NOT_SUPPORTED;
            
            matmul_impl_id = *id;
            return CPHVB_SUCCESS;			
        }
        else
        {
        	*id = matmul_impl_id;
        	return CPHVB_SUCCESS;
        }
    }
    
    return CPHVB_USERFUNC_NOT_SUPPORTED;
}

cphvb_error cphvb_reduce( cphvb_userfunc *arg, void* ve_arg)
{
    return cphvb_compute_reduce( arg, ve_arg );
}

cphvb_error cphvb_random( cphvb_userfunc *arg, void* ve_arg)
{
    return cphvb_compute_random( arg, ve_arg );
}

cphvb_error cphvb_matmul( cphvb_userfunc *arg, void* ve_arg)
{
    return cphvb_compute_matmul( arg, ve_arg );
    
}
