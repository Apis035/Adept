
#ifndef _ISAAC_COMPILER_H
#define _ISAAC_COMPILER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
    ================================ compiler.h ================================
    Module for encapsulating the compiler
    ----------------------------------------------------------------------------
*/

#include <stdarg.h>
#include <stdbool.h>

#include "AST/ast.h"
#include "AST/ast_expr.h"
#include "AST/ast_type_lean.h"
#include "DRVR/config.h"
#include "DRVR/object.h"
#include "UTIL/ground.h"
#include "UTIL/index_id_list.h"
#include "UTIL/string_builder.h"
#include "UTIL/string_list.h"
#include "UTIL/trait.h"

// Possible compiler trait options
#define COMPILER_SHOW_CONSOLE             TRAIT_1
#define COMPILER_MAKE_PACKAGE             TRAIT_2
#define COMPILER_INFLATE_PACKAGE          TRAIT_3
#define COMPILER_EXECUTE_RESULT           TRAIT_4
#define COMPILER_DEBUG_SYMBOLS            TRAIT_5
#define COMPILER_NO_WARN                  TRAIT_6
#define COMPILER_NO_UNDEF                 TRAIT_7
#define COMPILER_NO_TYPEINFO              TRAIT_8
#define COMPILER_NO_REMOVE_OBJECT         TRAIT_A
#define COMPILER_UNSAFE_META              TRAIT_B
#define COMPILER_UNSAFE_NEW               TRAIT_C
#define COMPILER_FUSSY                    TRAIT_D
#define COMPILER_FORCE_STDLIB             TRAIT_E
#define COMPILER_WARN_AS_ERROR            TRAIT_F
#define COMPILER_SHORT_WARNINGS           TRAIT_G
#define COMPILER_EMIT_OBJECT              TRAIT_2_1
#define COMPILER_COLON_COLON              TRAIT_2_2
#define COMPILER_TYPE_COLON               TRAIT_2_3
#define COMPILER_WINDOWED                 TRAIT_2_4
#define COMPILER_OUTPUT_DYNAMIC_LIBRARY   TRAIT_2_5

// Possible compiler trait checks
#define COMPILER_NULL_CHECKS      TRAIT_1
#define COMPILER_LEAK_CHECKS      TRAIT_2
#define COMPILER_BOUNDS_CHECKS    TRAIT_3

// Possible compiler errors/warning to ignore
#define COMPILER_IGNORE_DEPRECATION             TRAIT_1
#define COMPILER_IGNORE_PARTIAL_SUPPORT         TRAIT_2
#define COMPILER_IGNORE_EARLY_RETURN            TRAIT_3
#define COMPILER_IGNORE_UNRECOGNIZED_DIRECTIVES TRAIT_3
#define COMPILER_IGNORE_OBSOLETE                TRAIT_4
#define COMPILER_IGNORE_UNUSED                  TRAIT_5
#define COMPILER_IGNORE_ALL                     TRAIT_ALL

// Possible optimization levels
#define OPTIMIZATION_NONE               0x00
#define OPTIMIZATION_LESS               0x01
#define OPTIMIZATION_DEFAULT            0x02
#define OPTIMIZATION_AGGRESSIVE         0x03
#define OPTIMIZATION_ABSOLUTELY_NOTHING 0x04

// Possible compiler debug trait options
#define COMPILER_DEBUG_STAGES          TRAIT_1
#define COMPILER_DEBUG_DUMP            TRAIT_2
#define COMPILER_DEBUG_LLVMIR          TRAIT_3
#define COMPILER_DEBUG_NO_VERIFICATION TRAIT_4
#define COMPILER_DEBUG_NO_RESULT       TRAIT_5

// Possible compiler result flags (for internal use)
#define COMPILER_RESULT_NONE                    TRAIT_NONE
#define COMPILER_RESULT_SUCCESS                 TRAIT_1 // Signifies success

// ---------------- adept_error_t, adept_warning_t ----------------
// Short result warning/error message
typedef struct {
    strong_cstr_t message;
    source_t source;
} adept_error_t, adept_warning_t;

// ---------------- compiler_t ----------------
// Structure that encapsulates the compiler
typedef struct compiler {
    char *location;      // Compiler Filename
    char *root;          // Compiler Root Directory (with /)

    object_t **objects;
    length_t objects_length;
    length_t objects_capacity;

    // Compiler persistent configuration options
    config_t config;
    maybe_null_strong_cstr_t config_filename;

    // Compiler command-line configuration options
    trait_t traits;            // COMPILER_* options
    char *output_filename;     // owned c-string
    unsigned int optimization; // 0 - 3 using OPTIMIZATION_* constants
    trait_t result_flags;      // Results flag (for internal use)
    trait_t checks;
    trait_t ignore;
    troolean use_pic;          // Generate using PIC relocation model
    bool use_libm;             // Link to libm using '-lm'
    bool extract_import_order;   // Parse file to extract order of all imported files
    trait_t debug_traits;      // COMPILER_DEBUG_* options

    // Default standard library to import from (global version)
    // If NULL, then use ADEPT_VERSION_STRING
    maybe_null_weak_cstr_t default_stdlib;

    adept_error_t *error;
    adept_warning_t *warnings;
    length_t warnings_length;
    length_t warnings_capacity;

    bool show_unused_variables_how_to_disable;
    unsigned int cross_compile_for;
    
    weak_cstr_t entry_point;
    string_builder_t user_linker_options;
    strong_cstr_list_t user_search_paths;
    strong_cstr_list_t windows_resources;

    weak_cstr_t init_point;
    weak_cstr_t deinit_point;
} compiler_t;

#define CROSS_COMPILE_NONE    0x00
#define CROSS_COMPILE_WINDOWS 0x01
#define CROSS_COMPILE_MACOS   0x02
#define CROSS_COMPILE_WASM32  0x03
#define CROSS_COMPILE_LINUX   0x04

// ---------------- compiler_run ----------------
// Runs a compiler with the given arguments.
errorcode_t compiler_run(compiler_t *compiler, int argc, char **argv);

// ---------------- compiler_invoke ----------------
// Invokes a compiler with arguments.
// NOTE: This is for internal use!  Use compiler_run() instead!
void compiler_invoke(compiler_t *compiler, int argc, char **argv);

// ---------------- handle_package_management ----------------
// Handles package management part of the compiler
// Returns true if package management was invoked by user
bool handle_package_management(compiler_t *compiler, int argc, char **argv);

// ---------------- compiler_init ----------------
// Initializes a compiler
void compiler_init(compiler_t *compiler);

// ---------------- compiler_free ----------------
// Frees data within a compiler
void compiler_free(compiler_t *compiler);

// ---------------- compiler_free_objects ----------------
// Frees objects of a compiler and resets 'objects_*' values
void compiler_free_objects(compiler_t *compiler);

// ---------------- compiler_free_error ----------------
// Frees error from the compiler
void compiler_free_error(compiler_t *compiler);

// ---------------- compiler_free_warnings ----------------
// Frees warnings from the compiler
void compiler_free_warnings(compiler_t *compiler);

// ---------------- compiler_new_object ----------------
// Generates a new object within the compiler
object_t *compiler_new_object(compiler_t *compiler);

// ---------------- parse_arguments ----------------
// Configures a compiler based on program arguments.
// argv[0] is ignored
errorcode_t parse_arguments(compiler_t *compiler, object_t *object, int argc, char **argv);

// ---------------- break_into_arguments ----------------
// Breaks a string into pseudo program arguments
// (*out_argv)[0] will be a blank constant c-string
void break_into_arguments(const char *s, int *out_argc, char ***out_argv);

// ---------------- show_help ----------------
// Displays program help information
void show_help(bool show_advanced_options);

// ---------------- show_how_to_ignore_unused_variables ----------------
// Displays information on how to ignore unused variables
void show_how_to_ignore_unused_variables(void);

// ---------------- show_version ----------------
// Displays program version information
void show_version(compiler_t *compiler);

// ---------------- show_root ----------------
// Displays root folder information
void show_root(compiler_t *compiler);

// ---------------- compiler_get_string ----------------
// Gets the string identifier of the compiler
strong_cstr_t compiler_get_string(void);

// ---------------- compiler_add_user_linker_option ----------------
// Adds user-supplied linker option
void compiler_add_user_linker_option(compiler_t *compiler, weak_cstr_t option);

// ---------------- compiler_add_user_search_path ----------------
// Adds user-supplied search path
void compiler_add_user_search_path(compiler_t *compiler, weak_cstr_t search_path, maybe_null_weak_cstr_t current_file);

// ---------------- compiler_create_package ----------------
// Creates and exports a package
errorcode_t compiler_create_package(compiler_t *compiler, object_t *object);

// ---------------- compiler_read_file ----------------
// Reads either a package or adept code file into tokens for an object
errorcode_t compiler_read_file(compiler_t *compiler, object_t *object);

// ---------------- compiler_get_stdlib ----------------
// Returns the current stdlib folder, including the preceding slash
strong_cstr_t compiler_get_stdlib(compiler_t *compiler, object_t *optional_object);

// ---------------- compiler_print_source ----------------
// Prints the source code at a given 'source_t'
void compiler_print_source(compiler_t *compiler, int line, source_t source);

// ---------------- compiler_panic (and friends) ----------------
// Prints a compiler error message at a given 'source_t'
void compiler_panic(compiler_t *compiler, source_t source, const char *message);
void compiler_panicf(compiler_t *compiler, source_t source, const char *format, ...);
void compiler_vpanicf(compiler_t *compiler, source_t source, const char *format, va_list args);

// ---------------- compiler_warn (and friends) ----------------
// Prints a compiler warning at a given 'source_t'
// Returns whether the compiler should exit, if return value of the function isn't void
bool compiler_warn(compiler_t *compiler, source_t source, const char *message);
bool compiler_warnf(compiler_t *compiler, source_t source, const char *format, ...);
void compiler_vwarnf(compiler_t *compiler, source_t source, const char *format, va_list args);

// ---------------- compiler_weak_panic (and friends) ----------------
#ifdef ADEPT_INSIGHT_BUILD
#define compiler_weak_panic(...)  compiler_warn(__VA_ARGS__)
#define compiler_weak_panicf(...) compiler_warnf(__VA_ARGS__)
#else
#define compiler_weak_panic(...)  ((void) compiler_panic(__VA_ARGS__), true)
#define compiler_weak_panicf(...) ((void) compiler_panicf(__VA_ARGS__), true)
#endif

#if !defined(ADEPT_INSIGHT_BUILD) || defined(__EMSCRIPTEN__)
// ---------------- compiler_undeclared_function ----------------
// Prints an error message for an undeclared function
// NOTE: 'gives' can be NULL or 'gives.elements_length' can be zero
//       to indicate to return matching
void compiler_undeclared_function(compiler_t *compiler, object_t *object, source_t source,
    weak_cstr_t name, ast_type_t *types, length_t arity, ast_type_t *gives, bool is_method);

// ---------------- compiler_possibilities ----------------
// Returns a list of IDs of possible AST functions that
// are similar the supplied parameters
// NOTE: If 'methods_only_type_of_this' is not NULL, then only
// methods with a 'this' type of it will be included.
func_id_list_t compiler_possibilities(compiler_t *compiler, object_t *object, weak_cstr_t name, ast_type_t *methods_only_type_of_this);
#endif

// ---------------- print_candidate ----------------
// Prints a function/method candidate
void print_candidate(ast_func_t *ast_func);

// ---------------- make_args_string ----------------
// Helper function for generating a string for function arguments
strong_cstr_t make_args_string(ast_type_t *types, ast_expr_t **defaults, length_t arity, trait_t traits);

// ---------------- object_panic_plain ----------------
// Prints a plain compiler error given an object
void object_panic_plain(object_t *object, const char *message);

// ---------------- object_panic_plain ----------------
// Prints a plain compiler error given an object
void object_panicf_plain(object_t *object, const char *message, ...);

// ---------------- adept_error_create ----------------
// Returns a newly allocated and initialized 'adept_error_t'
adept_error_t *adept_error_create(strong_cstr_t message, source_t source);

// ---------------- adept_error_free_fully ----------------
// Frees the memory owned by an 'adept_error_t' and the memory occupied by it
void adept_error_free_fully(adept_error_t *error);

// ---------------- compiler_create_warning ----------------
// Adds a warning to the list of warnings for a compiler
void compiler_create_warning(compiler_t *compiler, strong_cstr_t message, source_t source);

// ---------------- adept_warnings_free_fully ----------------
// Frees an array of warnings and the memory it occupies
void adept_warnings_free_fully(adept_warning_t *warnings, length_t length);

// ---------------- compiler_unnamespaced_name ----------------
// Returns weak C-string to unnamespaced version of an identifier
// e.g. "namespace\thing" => 'thing'
weak_cstr_t compiler_unnamespaced_name(weak_cstr_t input);

#ifdef __cplusplus
}
#endif

#endif // _ISAAC_COMPILER_H
