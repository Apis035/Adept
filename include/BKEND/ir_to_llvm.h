
#ifndef _ISAAC_IR_TO_LLVM_H
#define _ISAAC_IR_TO_LLVM_H

/*
    =============================== ir_to_llvm.h ==============================
    Module for exporting intermediate representation to LLVM
    ---------------------------------------------------------------------------
*/

#include "DRVR/compiler.h"
#include <llvm-c/TargetMachine.h>

// ---------------- value_catalog_block_t ----------------
// Contains a list of resulting values from every
// instruction within a basic block
typedef struct { LLVMValueRef *value_references; } value_catalog_block_t;

// ---------------- value_catalog_t ----------------
// Contains a 'value_catalog_block_t' list that
// holds the resulting values from every instruction
// of every block in a function
typedef struct { value_catalog_block_t *blocks; length_t blocks_length; } value_catalog_t;

// ---------------- varstack_t ----------------
// A list of stack variables for a function
typedef struct { LLVMValueRef *values; LLVMTypeRef *types; length_t length; } varstack_t;

// ---------------- llvm_string_table_entry_t ----------------
// An entry in the string table
typedef struct { weak_cstr_t data; length_t length; LLVMValueRef global_data; } llvm_string_table_entry_t;

typedef struct {
    llvm_string_table_entry_t *entries;
    length_t length;
    length_t capacity;
} llvm_string_table_t;

typedef struct {
    LLVMValueRef phi;
    LLVMValueRef a;
    LLVMValueRef b;
    length_t basicblock_a;
    length_t basicblock_b;
} llvm_phi2_relocation_t;

typedef struct {
    llvm_phi2_relocation_t *unrelocated;
    length_t length;
    length_t capacity;
} llvm_phi2_relocation_list_t;

typedef struct {
    LLVMValueRef global;
    LLVMTypeRef type;
} llvm_static_global_t;

// ---------------- llvm_context_t ----------------
// A general container for the LLVM exporting context
typedef struct {
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    value_catalog_t *catalog;
    varstack_t *stack;
    LLVMValueRef *func_skeletons;
    LLVMValueRef *global_variables;
    LLVMValueRef *anon_global_variables;
    LLVMTargetDataRef data_layout;
    LLVMValueRef memcpy_intrinsic;
    LLVMValueRef memset_intrinsic;
    LLVMValueRef stacksave_intrinsic;
    LLVMValueRef stackrestore_intrinsic;
    LLVMValueRef va_start_intrinsic;
    LLVMValueRef va_end_intrinsic;
    LLVMValueRef va_copy_intrinsic;
    compiler_t *compiler;
    object_t *object;

    // Variables only used for compilation with null checks
    LLVMBasicBlockRef null_check_on_fail_block;
    LLVMValueRef line_phi;
    LLVMValueRef column_phi;
    LLVMValueRef null_check_failure_message_bytes;
    bool has_null_check_failure_message_bytes;

    llvm_string_table_t string_table;
    llvm_phi2_relocation_list_t relocation_list;

    llvm_static_global_t *static_globals;
    length_t static_globals_length;
    length_t static_globals_capacity;
    LLVMBasicBlockRef static_globals_initialization_routine;
    LLVMBasicBlockRef static_globals_initialization_post;
    LLVMValueRef static_globals_deinitialization_function;
    LLVMBasicBlockRef boot;

    LLVMTypeRef i64_type;
    LLVMTypeRef f64_type;
} llvm_context_t;

// ---------------- ir_to_llvm_type ----------------
// Converts an IR type to an LLVM type
LLVMTypeRef ir_to_llvm_type(llvm_context_t *llvm, ir_type_t *ir_type);

// ---------------- ir_to_llvm_value ----------------
// Converts an IR value to an LLVM value
LLVMValueRef ir_to_llvm_value(llvm_context_t *llvm, ir_value_t *value);

// ---------------- ir_to_llvm_functions ----------------
// Generates LLVM function skeletons for IR functions
errorcode_t ir_to_llvm_functions(llvm_context_t *llvm, object_t *object);

// ---------------- ir_to_llvm_function_bodies ----------------
// Generates LLVM function bodies for IR functions
errorcode_t ir_to_llvm_function_bodies(llvm_context_t *llvm, object_t *object);

// ---------------- ir_to_llvm_instructions ----------------
// Generates LLVM instructions from IR instructions
errorcode_t ir_to_llvm_instructions(llvm_context_t *llvm, ir_instr_t **instructions, length_t instructions_length, length_t basicblock_id,
        length_t f, LLVMBasicBlockRef *llvm_blocks, LLVMBasicBlockRef *llvm_exit_blocks);

// ---------------- ir_to_llvm_basicblocks ----------------
// Generates LLVM basicblocks and instructions from IR basicblocks and instructions
errorcode_t ir_to_llvm_basicblocks(llvm_context_t *llvm, ir_basicblock_t *basicblocks, length_t basicblocks_length, LLVMValueRef func_skeleton,
        ir_func_t *module_func, LLVMBasicBlockRef *llvm_blocks, LLVMBasicBlockRef *llvm_exit_blocks, length_t f);

// ---------------- ir_to_llvm_allocate_stack_variables ----------------
// Generates stack variables
errorcode_t ir_to_llvm_allocate_stack_variables(llvm_context_t *llvm, varstack_t *stack, LLVMValueRef func_skeleton, ir_func_t *module_func);

// ---------------- build_llvm_null_check_on_failure_block ----------------
// Creates 'llvm->null_check_on_fail_block'
void build_llvm_null_check_on_failure_block(llvm_context_t *llvm, LLVMValueRef func_skeleton, ir_func_t *module_func);

// ---------------- ir_to_llvm_globals ----------------
// Generates LLVM globals for IR globals
errorcode_t ir_to_llvm_globals(llvm_context_t *llvm, object_t *object);

// ---------------- ir_to_llvm_null_check ----------------
// Generates LLVM instructions to check for null pointer
void ir_to_llvm_null_check(llvm_context_t *llvm, length_t func_skeleton_index, LLVMValueRef pointer, int line, int column, LLVMBasicBlockRef *out_landing_basicblock);

// ---------------- ir_to_llvm_config_optlvl ----------------
// Converts optimization level to LLVM optimization constant
LLVMCodeGenOptLevel ir_to_llvm_config_optlvl(compiler_t *compiler);

// ---------------- llvm_string_table_find ----------------
// Finds the global variable data for an entry in the string table,
// returns NULL if not found
LLVMValueRef llvm_string_table_find(llvm_string_table_t *table, weak_cstr_t array, length_t length);

// ---------------- llvm_string_table_add ----------------
// Adds an entry into the string table
void llvm_string_table_add(llvm_string_table_t *table, weak_cstr_t name, length_t length, LLVMValueRef global_data);

// ---------------- llvm_string_table_entry_cmp ----------------
// Compares two entries in the string table
int llvm_string_table_entry_cmp(const void *va, const void *vb);

// ---------------- llvm_create_static_variable ----------------
// Creates a static variable
LLVMValueRef llvm_create_static_variable(llvm_context_t *llvm, ir_type_t *type, ir_value_t *optional_initializer);

// ---------------- value_catalog_prepare ----------------
// Creates a value_catalog_t cabable of holding value results

void value_catalog_prepare(value_catalog_t *out_catalog, ir_basicblock_t *basicblocks, length_t basicblocks_length);

// ---------------- value_catalog_free ----------------
// Frees memory allocated by a value_catalog_t
void value_catalog_free(value_catalog_t *catalog);

// ---------------- ir_to_llvm_inject_init_built ----------------
// Injects basicblocks from 'init_builder' into the initialization point
errorcode_t ir_to_llvm_inject_init_built(llvm_context_t *llvm);

// ---------------- ir_to_llvm_inject_deinit_built ----------------
// Injects basicblocks from 'deinit_builder' into the initialization point
errorcode_t ir_to_llvm_inject_deinit_built(llvm_context_t *llvm);

// ---------------- ir_to_llvm_generate_deinit_svars_function_head ----------------
// Creates function head for the function that will handle deinitialization
// of all static variables
errorcode_t ir_to_llvm_generate_deinit_svars_function_head(llvm_context_t *llvm);

#endif // _ISAAC_IR_TO_LLVM_H
