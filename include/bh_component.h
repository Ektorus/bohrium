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

#ifndef __BH_INTERFACE_H
#define __BH_INTERFACE_H

#include <bh_type.h>
#include <bh_instruction.h>
#include <bh_opcode.h>
#include <bh_error.h>
#include <bh_iniparser.h>
#include <bh_win.h>
#include <bh_ir.h>

#ifdef __cplusplus
extern "C" {
#endif

//Maximum number of characters in the name of a component, a attribute or
//a function.
#define BH_COMPONENT_NAME_SIZE (1024)

//Maximum number of support childs for a component
#define BH_COMPONENT_MAX_CHILDS (6)

/* Initialize the component
 *
 * @name    The name of the component e.g. node
 * @return  Error codes (BH_SUCCESS, BH_ERROR, BH_OUT_OF_MEMORY)
 */
typedef bh_error (*bh_init)(const char *name);

/* Shutdown the component, which include a instruction flush
 *
 * @return Error codes (BH_SUCCESS, BH_ERROR)
 */
typedef bh_error (*bh_shutdown)(void);

/* Execute a BhIR (graph of instructions)
 *
 * @bhir    The BhIR to execute
 * @return  Error codes (BH_SUCCESS, BH_ERROR, BH_OUT_OF_MEMORY)
 */
typedef bh_error (*bh_execute)(bh_ir* bhir);

/* Register a new extension method.
 *
 * @name   Name of the function e.g. matmul
 * @opcode Opcode for the new function.
 * @return Error codes (BH_SUCCESS, BH_ERROR, BH_OUT_OF_MEMORY,
 *                      BH_EXTMETHOD_NOT_SUPPORTED)
 */
typedef bh_error (*bh_extmethod)(const char *name, bh_opcode opcode);

/* Extension method prototype implementation.
 *
 * @instr  The extension method instruction to handle
 * @arg    Additional component specific argument
 * @return Error codes (BH_SUCCESS, BH_ERROR, BH_OUT_OF_MEMORY,
 *                      BH_TYPE_NOT_SUPPORTED)
 */
typedef bh_error (*bh_extmethod_impl)(bh_instruction *instr, void* arg);

/* The interface functions of a component */
typedef struct
{
    char name[BH_COMPONENT_NAME_SIZE];      // Name of the component
    void *lib_handle;                       // Handle for the dynamic linked library.
    bh_init       init;                     // The interface function pointers
    bh_shutdown   shutdown;
    bh_execute    execute;
    bh_extmethod  extmethod;
} bh_component_iface;

/* Codes for known component types */
typedef enum
{
    BH_BRIDGE,
    BH_VEM,
    BH_VE,
    BH_FILTER,
    BH_FUSER,
    BH_STACK,
    BH_COMPONENT_ERROR
} bh_component_type;

/* The component object */
typedef struct
{
    char name[BH_COMPONENT_NAME_SIZE];      // Name of the component
    dictionary *config;                     // The ini-config dictionary
    bh_component_type type;                 // The component type
    bh_intp nchildren;                      // Number of children
    // The interface of the children of this component
    bh_component_iface children[BH_COMPONENT_MAX_CHILDS];
} bh_component;

/* Initilize the component object
 *
 * @self   The component object to initilize
 * @name   The name of the component. If NULL "bridge" will be used.
 * @return Error codes (BH_SUCCESS, BH_ERROR)
 */
DLLEXPORT bh_error bh_component_init(bh_component *self, const char* name);

/* Destroyes the component object.
 *
 * @self   The component object to destroy
 */
DLLEXPORT void bh_component_destroy(bh_component *self);

/* Retrieves an extension method implementation.
 *
 * @self      The component object.
 * @name      Name of the extension method e.g. matmul
 * @extmethod Pointer to the method (output)
 * @return    Error codes (BH_SUCCESS, BH_ERROR, BH_OUT_OF_MEMORY,
 *                         BH_EXTMETHOD_NOT_SUPPORTED)
 */
DLLEXPORT bh_error bh_component_extmethod(const bh_component *self,
                                          const char *name,
                                          bh_extmethod_impl *extmethod);

/* Look up a key in the config file
 *
 * @component The component.
 * @key       The key to lookup in the config file
 * @return    The value if found, otherwise NULL
 */
DLLEXPORT char* bh_component_config_lookup(const bh_component *component,
                                           const char* key);
DLLEXPORT bool bh_component_config_lookup_bool(const bh_component *component,
                                               const char* key, bool notfound);
DLLEXPORT int bh_component_config_lookup_int(const bh_component *component,
                                             const char* key, int notfound);
DLLEXPORT double bh_component_config_lookup_double(const bh_component *component,
                                                   const char* key, double notfound);

/**
 *  Grab an bool-valued configuration option for the given component.
 *  The bool-value option must be specified as (0/1, true/false, yes/no).
 *
 *  @param component The component to grab the configuration option for.
 *  @param option_name Name of configuration option.
 *  @param option Pointer to store the option in.
 *
 *  @return BH_ERROR if is not within bounds, does not exists etc. BH_SUCCESS othervise.
 */
DLLEXPORT bh_error bh_component_config_bool_option(const bh_component* component,
                                                   const char* option_name,
                                                   bool* option);

/**
 *  Grab an int-valued configuration option for the given component.
 *  The int-value option must be within the range [min, max].
 *
 *  @param component The component to grab the configuration option for.
 *  @param option_name Name of configuration option.
 *  @param min Lower bound on the integer.
 *  @param max Upper bound on the integer.
 *  @param option Pointer to store the option in.
 *
 *  @return BH_ERROR if is not within bounds, does not exists etc. BH_SUCCESS othervise.
 */
DLLEXPORT bh_error bh_component_config_int_option(const bh_component* component,
                                                  const char* option_name,
                                                  int min,
                                                  int max,
                                                  bh_intp* option);

/**
 *  Grabs a string-valued configuration option for the given component.
 *
 *  @param component The component to grab the configuration option for.
 *  @param option_name Name of configuration option.
 *  @param option Pointer to store the option in.
 *
 *  @return BH_ERROR if option does not exists. BH_SUCCESS othervise.
 */
DLLEXPORT bh_error bh_component_config_string_option(const bh_component* component,
                                                     const char* option_name,
                                                     char** option);

/**
 *  Grabs a string-valued "path to directory" configuration option for the given component.
 *
 *  @param component The component to grab the configuration option for.
 *  @param option_name Name of configuration option.
 *  @param option Pointer to store the option in.
 *
 *  @return BH_ERROR if the path is not a valid directory does not exists etc. BH_SUCCESS othervise.
 */
DLLEXPORT bh_error bh_component_config_path_option(const bh_component* component,
                                                   const char* option_name,
                                                   char** option);

#ifdef __cplusplus
}
#endif

#endif

