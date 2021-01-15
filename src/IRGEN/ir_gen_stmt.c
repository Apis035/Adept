
#include "UTIL/util.h"
#include "UTIL/color.h"
#include "UTIL/ground.h"
#include "UTIL/filename.h"
#include "UTIL/builtin_type.h"
#include "IRGEN/ir_gen.h"
#include "IRGEN/ir_gen_expr.h"
#include "IRGEN/ir_gen_find.h"
#include "IRGEN/ir_gen_stmt.h"
#include "IRGEN/ir_gen_type.h"
#include "BRIDGE/bridge.h"

errorcode_t ir_gen_stmts(ir_builder_t *builder, ast_expr_t **statements, length_t statements_length, bool *out_is_terminated){
    ir_instr_t *built_instr;
    ir_value_t *expression_value = NULL;
    ast_type_t temporary_type;

    // Default value of whether the statements were terminated is 'false'
    if(out_is_terminated) *out_is_terminated = false;

    for(length_t s = 0; s != statements_length; s++){
        ast_expr_t *stmt = statements[s];

        switch(stmt->id){
        case EXPR_RETURN:
            // Generate instructions in order to return
            if(ir_gen_stmt_return(builder, (ast_expr_return_t*) stmt, out_is_terminated)) return FAILURE;

            // Warn if there are statements following
            if(s + 1 != statements_length){
                bool should_show_early_return_warning = builder->compiler->traits & COMPILER_FUSSY && !(builder->compiler->ignore & COMPILER_IGNORE_EARLY_RETURN);

                if(should_show_early_return_warning){
                    const char *f_name = builder->object->ast.funcs[builder->ast_func_id].name;

                    if(compiler_warnf(builder->compiler, statements[s + 1]->source, "Statements after 'return' in function '%s'", f_name))
                        return FAILURE;
                }
            }

            // Return since no other statements can be after this one
            return SUCCESS;
        case EXPR_CALL:
        case EXPR_CALL_METHOD:
            if(ir_gen_stmt_call_like(builder, stmt)) return FAILURE;
            break;
        case EXPR_PREINCREMENT:
        case EXPR_PREDECREMENT:
        case EXPR_POSTINCREMENT:
        case EXPR_POSTDECREMENT:
        case EXPR_TOGGLE:
            // Expression-statements will be processed elsewhere
            if(ir_gen_expr(builder, stmt, NULL, true, NULL)) return FAILURE;
            break;
        case EXPR_DECLARE: case EXPR_DECLAREUNDEF:
            if(ir_gen_stmt_declare(builder, (ast_expr_declare_t*) stmt)) return FAILURE;
            break;
        case EXPR_ASSIGN: case EXPR_ADD_ASSIGN: case EXPR_SUBTRACT_ASSIGN:
        case EXPR_MULTIPLY_ASSIGN: case EXPR_DIVIDE_ASSIGN: case EXPR_MODULUS_ASSIGN:
        case EXPR_AND_ASSIGN: case EXPR_OR_ASSIGN: case EXPR_XOR_ASSIGN:
        case EXPR_LS_ASSIGN: case EXPR_RS_ASSIGN:
        case EXPR_LGC_LS_ASSIGN: case EXPR_LGC_RS_ASSIGN: {
                unsigned int assignment_type = stmt->id;
                ast_expr_assign_t *assign_stmt = ((ast_expr_assign_t*) stmt);
                ir_value_t *destination;
                ast_type_t destination_type, expression_value_type;
                if(ir_gen_expr(builder, assign_stmt->value, &expression_value, false, &expression_value_type)) return FAILURE;
                if(ir_gen_expr(builder, assign_stmt->destination, &destination, true, &destination_type)){
                    ast_type_free(&expression_value_type);
                    return FAILURE;
                }

                if(!ast_types_conform(builder, &expression_value, &expression_value_type, &destination_type, CONFORM_MODE_CALCULATION)){
                    char *a_type_str = ast_type_str(&expression_value_type);
                    char *b_type_str = ast_type_str(&destination_type);
                    compiler_panicf(builder->compiler, assign_stmt->source, "Incompatible types '%s' and '%s'", a_type_str, b_type_str);
                    free(a_type_str);
                    free(b_type_str);
                    ast_type_free(&destination_type);
                    ast_type_free(&expression_value_type);
                    return FAILURE;
                }

                ir_value_t *instr_value;

                if(assignment_type == EXPR_ASSIGN){
                    if(assign_stmt->is_pod || !handle_assign_management(builder, expression_value, &expression_value_type, destination, &destination_type, false)){
                        build_store(builder, expression_value, destination, stmt->source);
                    }

                    ast_type_free(&destination_type);
                    ast_type_free(&expression_value_type);
                } else {
                    ast_type_free(&destination_type);
                    ast_type_free(&expression_value_type);

                    instr_value = build_load(builder, destination, stmt->source);

                    ir_value_result_t *value_result;
                    value_result = ir_pool_alloc(builder->pool, sizeof(ir_value_result_t));
                    value_result->block_id = builder->current_block_id;
                    value_result->instruction_id = builder->current_block->instructions_length;

                    built_instr = build_instruction(builder, sizeof(ir_instr_math_t));
                    ((ir_instr_math_t*) built_instr)->result_type = expression_value->type;
                    ((ir_instr_math_t*) built_instr)->a = instr_value;
                    ((ir_instr_math_t*) built_instr)->b = expression_value;

                    switch(assignment_type){
                    case EXPR_ADD_ASSIGN:
                        if(i_vs_f_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_ADD, INSTRUCTION_FADD)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't add those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_SUBTRACT_ASSIGN:
                        if(i_vs_f_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_SUBTRACT, INSTRUCTION_FSUBTRACT)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't subtract those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_MULTIPLY_ASSIGN:
                        if(i_vs_f_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_MULTIPLY, INSTRUCTION_FMULTIPLY)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't multiply those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_DIVIDE_ASSIGN:
                        if(u_vs_s_vs_float_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_UDIVIDE, INSTRUCTION_SDIVIDE, INSTRUCTION_FDIVIDE)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't divide those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_MODULUS_ASSIGN:
                        if(u_vs_s_vs_float_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_UMODULUS, INSTRUCTION_SMODULUS, INSTRUCTION_FMODULUS)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't take the modulus of those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_AND_ASSIGN:
                        if(i_vs_f_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_BIT_AND, INSTRUCTION_NONE)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't perform bitwise 'and' on those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_OR_ASSIGN:
                        if(i_vs_f_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_BIT_OR, INSTRUCTION_NONE)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't perform bitwise 'or' on those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_XOR_ASSIGN:
                        if(i_vs_f_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_BIT_XOR, INSTRUCTION_NONE)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't perform bitwise 'xor' on those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_LS_ASSIGN:
                        if(i_vs_f_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_BIT_LSHIFT, INSTRUCTION_NONE)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't perform bitwise 'left shift' on those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_RS_ASSIGN:
                        if(u_vs_s_vs_float_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_BIT_LGC_RSHIFT, INSTRUCTION_BIT_RSHIFT, INSTRUCTION_FMODULUS)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't perform bitwise 'right shift' on those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_LGC_LS_ASSIGN:
                        if(i_vs_f_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_BIT_LSHIFT, INSTRUCTION_NONE)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't perform bitwise 'logical left shift' on those types");
                            return FAILURE;
                        }
                        break;
                    case EXPR_LGC_RS_ASSIGN:
                        if(i_vs_f_instruction((ir_instr_math_t*) built_instr, INSTRUCTION_BIT_LGC_RSHIFT, INSTRUCTION_NONE)){
                            compiler_panic(builder->compiler, assign_stmt->source, "Can't perform bitwise 'logical right shift' on those types");
                            return FAILURE;
                        }
                        break;
                    default:
                        compiler_panic(builder->compiler, assign_stmt->source, "INTERNAL ERROR: ir_gen_stmts() got unknown assignment operator id");
                        return FAILURE;
                    }

                    build_store(builder, build_value_from_prev_instruction(builder), destination, stmt->source);
                }
            }
            break;
        case EXPR_IF: case EXPR_UNLESS: {
                unsigned int conditional_type = stmt->id;
                if(ir_gen_expr(builder, ((ast_expr_if_t*) stmt)->value, &expression_value, false, &temporary_type)) return FAILURE;

                if(!ast_types_conform(builder, &expression_value, &temporary_type, &builder->static_bool, CONFORM_MODE_CALCULATION)){
                    char *a_type_str = ast_type_str(&temporary_type);
                    char *b_type_str = ast_type_str(&builder->static_bool);
                    compiler_panicf(builder->compiler, stmt->source, "Received type '%s' when conditional expects type '%s'", a_type_str, b_type_str);
                    free(a_type_str);
                    free(b_type_str);
                    ast_type_free(&temporary_type);
                    return FAILURE;
                }

                ast_type_free(&temporary_type);

                length_t new_basicblock_id = build_basicblock(builder);
                length_t end_basicblock_id = build_basicblock(builder);

                if(conditional_type == EXPR_IF){
                    build_cond_break(builder, expression_value, new_basicblock_id, end_basicblock_id);
                } else {
                    build_cond_break(builder, expression_value, end_basicblock_id, new_basicblock_id);
                }

                ast_expr_t **if_stmts = ((ast_expr_if_t*) stmt)->statements;
                length_t if_stmts_length = ((ast_expr_if_t*) stmt)->statements_length;
                bool terminated;

                open_scope(builder);
                build_using_basicblock(builder, new_basicblock_id);
                if(ir_gen_stmts(builder, if_stmts, if_stmts_length, &terminated)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!terminated){
                    handle_deference_for_variables(builder, &builder->scope->list);
                    build_break(builder, end_basicblock_id);
                }

                close_scope(builder);
                build_using_basicblock(builder, end_basicblock_id);
            }
            break;
        case EXPR_IFELSE: case EXPR_UNLESSELSE: {
                unsigned int conditional_type = stmt->id;
                if(ir_gen_expr(builder, ((ast_expr_ifelse_t*) stmt)->value, &expression_value, false, &temporary_type)) return FAILURE;

                if(!ast_types_conform(builder, &expression_value, &temporary_type, &builder->static_bool, CONFORM_MODE_CALCULATION)){
                    char *a_type_str = ast_type_str(&temporary_type);
                    char *b_type_str = ast_type_str(&builder->static_bool);
                    compiler_panicf(builder->compiler, stmt->source, "Received type '%s' when conditional expects type '%s'", a_type_str, b_type_str);
                    free(a_type_str);
                    free(b_type_str);
                    ast_type_free(&temporary_type);
                    return FAILURE;
                }

                ast_type_free(&temporary_type);

                length_t new_basicblock_id = build_basicblock(builder);
                length_t else_basicblock_id = build_basicblock(builder);
                length_t end_basicblock_id = build_basicblock(builder);

                if(conditional_type == EXPR_IFELSE){
                    build_cond_break(builder, expression_value, new_basicblock_id, else_basicblock_id);
                } else {
                    build_cond_break(builder, expression_value, else_basicblock_id, new_basicblock_id);
                }

                ast_expr_t **if_stmts = ((ast_expr_ifelse_t*) stmt)->statements;
                length_t if_stmts_length = ((ast_expr_ifelse_t*) stmt)->statements_length;
                bool terminated;

                open_scope(builder);
                build_using_basicblock(builder, new_basicblock_id);

                if(ir_gen_stmts(builder, if_stmts, if_stmts_length, &terminated)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!terminated){
                    handle_deference_for_variables(builder, &builder->scope->list);
                    build_break(builder, end_basicblock_id);
                }

                close_scope(builder);

                ast_expr_t **else_stmts = ((ast_expr_ifelse_t*) stmt)->else_statements;
                length_t else_stmts_length = ((ast_expr_ifelse_t*) stmt)->else_statements_length;

                open_scope(builder);
                build_using_basicblock(builder, else_basicblock_id);
                if(ir_gen_stmts(builder, else_stmts, else_stmts_length, &terminated)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!terminated){
                    handle_deference_for_variables(builder, &builder->scope->list);
                    build_break(builder, end_basicblock_id);
                }

                close_scope(builder);
                build_using_basicblock(builder, end_basicblock_id);
            }
            break;
        case EXPR_WHILE: case EXPR_UNTIL: {
                length_t test_basicblock_id = build_basicblock(builder);
                length_t new_basicblock_id = build_basicblock(builder);
                length_t end_basicblock_id = build_basicblock(builder);

                if(((ast_expr_while_t*) stmt)->label != NULL){
                    push_loop_label(builder, ((ast_expr_while_t*) stmt)->label, end_basicblock_id, test_basicblock_id);
                }

                length_t prev_break_block_id = builder->break_block_id;
                length_t prev_continue_block_id = builder->continue_block_id;
                bridge_scope_t *prev_break_continue_scope = builder->break_continue_scope;

                builder->break_block_id = end_basicblock_id;
                builder->continue_block_id = test_basicblock_id;
                builder->break_continue_scope = builder->scope;

                build_break(builder, test_basicblock_id);
                build_using_basicblock(builder, test_basicblock_id);

                unsigned int conditional_type = stmt->id;
                if(ir_gen_expr(builder, ((ast_expr_while_t*) stmt)->value, &expression_value, false, &temporary_type)) return FAILURE;

                // Create static bool type for comparison with
                ast_elem_base_t bool_base;
                bool_base.id = AST_ELEM_BASE;
                bool_base.source = NULL_SOURCE;
                bool_base.source.object_index = builder->object->index;
                bool_base.base = "bool";
                ast_elem_t *bool_type_elem = (ast_elem_t*) &bool_base;
                ast_type_t bool_type;
                bool_type.elements = &bool_type_elem;
                bool_type.elements_length = 1;
                bool_type.source = NULL_SOURCE;
                bool_type.source.object_index = builder->object->index;

                if(!ast_types_conform(builder, &expression_value, &temporary_type, &bool_type, CONFORM_MODE_CALCULATION)){
                    char *a_type_str = ast_type_str(&temporary_type);
                    char *b_type_str = ast_type_str(&bool_type);
                    compiler_panicf(builder->compiler, stmt->source, "Received type '%s' when conditional expects type '%s'", a_type_str, b_type_str);
                    free(a_type_str);
                    free(b_type_str);
                    ast_type_free(&temporary_type);
                    return FAILURE;
                }

                ast_type_free(&temporary_type);

                if(conditional_type == EXPR_WHILE){
                    build_cond_break(builder, expression_value, new_basicblock_id, end_basicblock_id);
                } else {
                    build_cond_break(builder, expression_value, end_basicblock_id, new_basicblock_id);
                }

                ast_expr_t **while_stmts = ((ast_expr_while_t*) stmt)->statements;
                length_t while_stmts_length = ((ast_expr_while_t*) stmt)->statements_length;
                bool terminated;

                open_scope(builder);
                build_using_basicblock(builder, new_basicblock_id);
                if(ir_gen_stmts(builder, while_stmts, while_stmts_length, &terminated)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!terminated){
                    handle_deference_for_variables(builder, &builder->scope->list);
                    build_break(builder, test_basicblock_id);
                }

                if(((ast_expr_while_t*) stmt)->label != NULL) pop_loop_label(builder);
                close_scope(builder);
                build_using_basicblock(builder, end_basicblock_id);

                builder->break_block_id = prev_break_block_id;
                builder->continue_block_id = prev_continue_block_id;
                builder->break_continue_scope = prev_break_continue_scope;
            }
            break;
        case EXPR_WHILECONTINUE: case EXPR_UNTILBREAK: {
                length_t new_basicblock_id = build_basicblock(builder);
                length_t end_basicblock_id = build_basicblock(builder);

                if(((ast_expr_whilecontinue_t*) stmt)->label != NULL){
                    push_loop_label(builder, ((ast_expr_whilecontinue_t*) stmt)->label, end_basicblock_id, new_basicblock_id);
                }

                length_t prev_break_block_id = builder->break_block_id;
                length_t prev_continue_block_id = builder->continue_block_id;
                bridge_scope_t *prev_break_continue_scope = builder->break_continue_scope;

                builder->break_block_id = end_basicblock_id;
                builder->continue_block_id = new_basicblock_id;
                builder->break_continue_scope = builder->scope;

                build_break(builder, new_basicblock_id);
                build_using_basicblock(builder, new_basicblock_id);

                ast_expr_t **loop_stmts = ((ast_expr_whilecontinue_t*) stmt)->statements;
                length_t loop_stmts_length = ((ast_expr_whilecontinue_t*) stmt)->statements_length;
                bool terminated;

                open_scope(builder);
                build_using_basicblock(builder, new_basicblock_id);
                if(ir_gen_stmts(builder, loop_stmts, loop_stmts_length, &terminated)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!terminated){
                    handle_deference_for_variables(builder, &builder->scope->list);

                    if(stmt->id == EXPR_WHILECONTINUE){
                        // 'while continue'
                        build_break(builder, end_basicblock_id);
                    } else {
                        // 'until break'
                        build_break(builder, new_basicblock_id);
                    }
                }

                if(((ast_expr_whilecontinue_t*) stmt)->label != NULL) pop_loop_label(builder);
                close_scope(builder);
                build_using_basicblock(builder, end_basicblock_id);

                builder->break_block_id = prev_break_block_id;
                builder->continue_block_id = prev_continue_block_id;
                builder->break_continue_scope = prev_break_continue_scope;
            }
            break;
        case EXPR_DELETE: {
                ast_expr_unary_t *delete_expr = (ast_expr_unary_t*) stmt;
                if(ir_gen_expr(builder, delete_expr->value, &expression_value, false, &temporary_type)) return FAILURE;

                if(temporary_type.elements_length == 0 || ( temporary_type.elements[0]->id != AST_ELEM_POINTER &&
                    !(temporary_type.elements[0]->id == AST_ELEM_BASE && strcmp(((ast_elem_base_t*) temporary_type.elements[0])->base, "ptr") == 0)
                )){
                    char *t = ast_type_str(&temporary_type);
                    compiler_panicf(builder->compiler, delete_expr->source, "Can't pass non-pointer type '%s' to delete", t);
                    ast_type_free(&temporary_type);
                    free(t);
                    return FAILURE;
                }

                built_instr = build_instruction(builder, sizeof(ir_instr_free_t));
                ((ir_instr_free_t*) built_instr)->id = INSTRUCTION_FREE;
                ((ir_instr_free_t*) built_instr)->result_type = NULL;
                ((ir_instr_free_t*) built_instr)->value = expression_value;
                ast_type_free(&temporary_type);
            }
            break;
        case EXPR_BREAK: {
                if(builder->break_block_id == 0){
                    compiler_panicf(builder->compiler, stmt->source, "Nowhere to break to");
                    return FAILURE;
                }

                bridge_scope_t *visit_scope;
                for(visit_scope = builder->scope; visit_scope->parent != builder->break_continue_scope; visit_scope = visit_scope->parent){
                    handle_deference_for_variables(builder, &visit_scope->list);
                }
                handle_deference_for_variables(builder, &visit_scope->list);
                build_break(builder, builder->break_block_id);
                if(out_is_terminated) *out_is_terminated = true;
            }
            return SUCCESS;
        case EXPR_CONTINUE: {
                if(builder->continue_block_id == 0){
                    compiler_panicf(builder->compiler, stmt->source, "Nowhere to continue to");
                    return FAILURE;
                }

                bridge_scope_t *visit_scope;
                for(visit_scope = builder->scope; visit_scope->parent != builder->break_continue_scope; visit_scope = visit_scope->parent){
                    handle_deference_for_variables(builder, &visit_scope->list);
                }
                handle_deference_for_variables(builder, &visit_scope->list);
                build_break(builder, builder->continue_block_id);
                if(out_is_terminated) *out_is_terminated = true;
            }
            return SUCCESS;
        case EXPR_FALLTHROUGH: {
                if(builder->fallthrough_block_id == 0){
                    compiler_panicf(builder->compiler, stmt->source, "Nowhere to fall through to");
                    return FAILURE;
                }

                bridge_scope_t *visit_scope;
                for(visit_scope = builder->scope; visit_scope->parent != builder->fallthrough_scope; visit_scope = visit_scope->parent){
                    handle_deference_for_variables(builder, &visit_scope->list);
                }
                handle_deference_for_variables(builder, &visit_scope->list);
                build_break(builder, builder->fallthrough_block_id);
                if(out_is_terminated) *out_is_terminated = true;
            }
            return SUCCESS;
        case EXPR_BREAK_TO: {
                char *target_label = ((ast_expr_break_to_t*) stmt)->label;
                length_t target_block_id = 0;
                bridge_scope_t *block_scope;

                for(length_t i = builder->block_stack_length; i != 0; i--){
                    if(strcmp(target_label, builder->block_stack_labels[i - 1]) == 0){
                        target_block_id = builder->block_stack_break_ids[i - 1];
                        block_scope = builder->block_stack_scopes[i - 1];
                        break;
                    }
                }

                if(target_block_id == 0){
                    compiler_panicf(builder->compiler, ((ast_expr_break_to_t*) stmt)->label_source, "Undeclared label '%s'", target_label);
                    return FAILURE;
                }

                bridge_scope_t *visit_scope;
                for(visit_scope = builder->scope; visit_scope->parent != block_scope; visit_scope = visit_scope->parent){
                    handle_deference_for_variables(builder, &visit_scope->list);
                }
                handle_deference_for_variables(builder, &visit_scope->list);
                build_break(builder, target_block_id);
                if(out_is_terminated) *out_is_terminated = true;
            }
            return SUCCESS;
        case EXPR_CONTINUE_TO: {
                char *target_label = ((ast_expr_continue_to_t*) stmt)->label;
                length_t target_block_id = 0;
                bridge_scope_t *block_scope;

                for(length_t i = builder->block_stack_length; i != 0; i--){
                    if(strcmp(target_label, builder->block_stack_labels[i - 1]) == 0){
                        target_block_id = builder->block_stack_continue_ids[i - 1];
                        block_scope = builder->block_stack_scopes[i - 1];
                        break;
                    }
                }

                if(target_block_id == 0){
                    compiler_panicf(builder->compiler, ((ast_expr_continue_to_t*) stmt)->label_source, "Undeclared label '%s'", target_label);
                    return FAILURE;
                }

                bridge_scope_t *visit_scope;
                for(visit_scope = builder->scope; visit_scope->parent != block_scope; visit_scope = visit_scope->parent){
                    handle_deference_for_variables(builder, &visit_scope->list);
                }
                handle_deference_for_variables(builder, &visit_scope->list);
                build_break(builder, target_block_id);
                if(out_is_terminated) *out_is_terminated = true;
            }
            return SUCCESS;
        case EXPR_EACH_IN: {
                // TODO: Clean up this really messy code

                ast_expr_each_in_t *each_in = (ast_expr_each_in_t*) stmt;

                length_t initial_basicblock_id = builder->current_block_id;
                length_t prep_basicblock_id = -1;

                ast_type_t *idx_ast_type = ast_get_usize(&builder->object->ast);

                ir_type_t *idx_ir_type = ir_builder_usize(builder);
                ir_type_t *idx_ir_type_ptr = ir_builder_usize_ptr(builder);

                open_scope(builder);

                // Create 'idx' variable
                length_t idx_var_id = builder->next_var_id;
                add_variable(builder, "idx", idx_ast_type, idx_ir_type, BRIDGE_VAR_POD | BRIDGE_VAR_UNDEF);
                ir_value_t *idx_ptr = build_lvarptr(builder, idx_ir_type_ptr, idx_var_id);

                // Set 'idx' to initial value of zero
                ir_value_t *initial_idx = ir_pool_alloc(builder->pool, sizeof(ir_value_t));
                initial_idx->value_type = VALUE_TYPE_LITERAL;
                initial_idx->type = idx_ir_type;
                initial_idx->extra = ir_pool_alloc(builder->pool, sizeof(unsigned long long));
                *((unsigned long long*) initial_idx->extra) = 0;

                build_store(builder, initial_idx, idx_ptr, stmt->source);

                if(!each_in->is_static){
                    prep_basicblock_id = build_basicblock(builder);
                    build_using_basicblock(builder, prep_basicblock_id);
                }
                
                // DANGEROUS: The following chunks of code assume that either 'each_in->list' isn't null or
                //        'each_in->array' and 'each_in->length' aren't null
                ir_value_t *list_precomputed = NULL;

                // NOTE: 'phantom_list_value.type' must be passed to ast_type_free if list_precomputed isn't NULL
                ast_expr_phantom_t phantom_list_value;

                if(each_in->list){
                    if(ir_gen_expr(builder, each_in->list, &list_precomputed, true, &phantom_list_value.type)){
                        return FAILURE;
                    }
                    phantom_list_value.id = EXPR_PHANTOM;
                    phantom_list_value.ir_value = list_precomputed;
                    phantom_list_value.source = each_in->list->source;
                    phantom_list_value.is_mutable = expr_is_mutable(each_in->list);
                }
                
                ir_value_t *fixed_array_value = NULL;

                // Generate length
                ir_value_t *array_length;
                if(list_precomputed){
                    if(ast_type_is_fixed_array(&phantom_list_value.type)){
                        // FIXED ARRAY
                        // Get array length from the type signature of the fixed array
                        // NOTE: Assumes element->id == AST_ELEM_FIXED_ARRAY because of earlier 'ast_type_is_fixed_array' call should've verified
                        ast_elem_fixed_array_t *fixed_array_element = (ast_elem_fixed_array_t*) phantom_list_value.type.elements[0];

                        // Verify that the fixed array type we got is good (just in case)
                        if(phantom_list_value.type.elements_length < 2){
                            compiler_panicf(builder->compiler, phantom_list_value.type.source, "INTERNAL ERROR: EXPR_EACH_IN got bad fixed array type in ir_gen_stmts");
                            ast_type_free(&phantom_list_value.type);
                            return FAILURE;
                        }

                        // DANGEROUS: Creating AST type that is a reference to parts of another
                        ast_type_t remaining_type;
                        remaining_type.elements = &phantom_list_value.type.elements[1];
                        remaining_type.elements_length = phantom_list_value.type.elements_length - 1;
                        remaining_type.source = phantom_list_value.type.elements[1]->source;

                        // Verify that the item type matches the item type provided
                        if(!ast_types_identical(&remaining_type, each_in->it_type)){
                            compiler_panic(builder->compiler, each_in->it_type->source,
                                "Element type doesn't match given array's element type");
        
                            char *s1 = ast_type_str(each_in->it_type);
                            char *s2 = ast_type_str(&remaining_type);
                            printf("(given element type : '%s', array element type : '%s')\n", s1, s2);
                            free(s1);
                            free(s2);
        
                            ast_type_free(&temporary_type);
                            return FAILURE;
                        }

                        // Verify that the value that was given is mutable
                        if(!phantom_list_value.is_mutable){
                            compiler_panicf(builder->compiler, phantom_list_value.type.source, "Fixed array given to 'each in' statement must be mutable");
                            ast_type_free(&phantom_list_value.type);
                            return FAILURE;
                        }

                        ir_type_t *item_ir_type;
                        if(ir_gen_resolve_type(builder->compiler, builder->object, &remaining_type, &item_ir_type)){
                            ast_type_free(&phantom_list_value.type);
                            return FAILURE;
                        }

                        fixed_array_value = build_bitcast(builder, list_precomputed, ir_type_pointer_to(builder->pool, item_ir_type));
                        array_length = build_literal_usize(builder->pool, fixed_array_element->length);
                    } else {
                        // STRUCTURE
                        // Get array length by calling the __length__() method

                        // TODO: CLEANUP: Clean up his very very dirty code

                        ast_expr_call_method_t length_call;
                        ast_expr_create_call_method_in_place(&length_call, "__length__", (ast_expr_t*) &phantom_list_value, 0, NULL, false, true, NULL, phantom_list_value.source);

                        if(ir_gen_expr(builder, (ast_expr_t*) &length_call, &array_length, false, &temporary_type)){
                            ast_type_free(&phantom_list_value.type);
                            return FAILURE;
                        }
                    }
                } else if(ir_gen_expr(builder, each_in->length, &array_length, false, &temporary_type)){
                    return FAILURE;
                }

                if(!fixed_array_value){
                    // Ensure the given value for the array length is of type 'usize'
                    if(!ast_types_conform(builder, &array_length, &temporary_type, idx_ast_type, CONFORM_MODE_CALCULATION)){
                        char *a_type_str = ast_type_str(&temporary_type);
                        compiler_panicf(builder->compiler, each_in->length->source, "Received type '%s' when array length should be 'usize'", a_type_str);
                        free(a_type_str);

                        if(list_precomputed) ast_type_free(&phantom_list_value.type);
                        ast_type_free(&temporary_type);
                        return FAILURE;
                    }

                    ast_type_free(&temporary_type);
                }

                if(each_in->is_static){
                    // Finally move ahead to prep basicblock for static each-in
                    prep_basicblock_id = build_basicblock(builder);
                    build_using_basicblock(builder, prep_basicblock_id);
                }

                // Generate (idx < length)
                ir_value_t *idx_value = build_load(builder, idx_ptr, stmt->source);
                ir_value_t *whether_keep_going_value = build_math(builder, INSTRUCTION_ULESSER, idx_value, array_length, ir_builder_bool(builder));

                // Generate body blocks
                length_t new_basicblock_id  = build_basicblock(builder);
                length_t inc_basicblock_id  = build_basicblock(builder);
                length_t end_basicblock_id  = build_basicblock(builder);

                // Hook up labels
                if(each_in->label != NULL){
                    push_loop_label(builder, each_in->label, end_basicblock_id, inc_basicblock_id);
                }
                
                length_t prev_break_block_id = builder->break_block_id;
                length_t prev_continue_block_id = builder->continue_block_id;
                bridge_scope_t *prev_break_continue_scope = builder->break_continue_scope;

                builder->break_block_id = end_basicblock_id;
                builder->continue_block_id = inc_basicblock_id;
                builder->break_continue_scope = builder->scope;

                // Generate conditional break
                build_cond_break(builder, whether_keep_going_value, new_basicblock_id, end_basicblock_id);

                if(each_in->is_static){
                    // Calculate array inside initial basicblock if static
                    build_using_basicblock(builder, initial_basicblock_id);
                } else {
                    // Calculate array inside new basicblock otherwise
                    build_using_basicblock(builder, new_basicblock_id);
                }

                // Generate array value
                ir_value_t *array;
                if(fixed_array_value){
                    // FIXED ARRAY
                    // We already have the value for the array (since we calculated it when doing fixed-array stuff)
                    array = fixed_array_value;
                } else if(list_precomputed){
                    // STRUCTURE
                    // Call the '__array__()' method to get the value for the array

                    // TODO: CLEANUP: Clean up his very very dirty code

                    ast_expr_call_method_t array_call;
                    ast_expr_create_call_method_in_place(&array_call, "__array__", (ast_expr_t*) &phantom_list_value, 0, NULL, false, true, NULL, phantom_list_value.source);

                    if(ir_gen_expr(builder, (ast_expr_t*) &array_call, &array, false, &temporary_type)){
                        ast_type_free(&phantom_list_value.type);
                        close_scope(builder);
                        return FAILURE;
                    }
                } else if(ir_gen_expr(builder, each_in->low_array, &array, false, &temporary_type)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!fixed_array_value){
                    // Ensure the given value for the array is of a pointer type
                    if(temporary_type.elements_length == 0 || temporary_type.elements[0]->id != AST_ELEM_POINTER){
                        compiler_panic(builder->compiler, each_in->low_array->source,
                            "Low-level array type for 'each in' statement must be a pointer");
                        ast_type_free(&temporary_type);
                        close_scope(builder);
                        return FAILURE;
                    }

                    // Get the item type
                    ast_type_dereference(&temporary_type);

                    // Ensure the item type matches the item type provided
                    if(!ast_types_identical(&temporary_type, each_in->it_type)){
                        compiler_panic(builder->compiler, each_in->it_type->source,
                            "Element type doesn't match given array's element type");

                        char *s1 = ast_type_str(each_in->it_type);
                        char *s2 = ast_type_str(&temporary_type);
                        printf("(given element type : '%s', array element type : '%s')\n", s1, s2);
                        free(s1);
                        free(s2);

                        ast_type_free(&temporary_type);
                        close_scope(builder);
                        return FAILURE;
                    }

                    ast_type_free(&temporary_type);
                }

                // Update 'it' inside new basicblock
                build_using_basicblock(builder, new_basicblock_id);

                length_t it_var_id = builder->next_var_id;
                char *it_name = each_in->it_name ? each_in->it_name : "it";

                // Generate new block statements to update 'it' variable
                add_variable(builder, it_name, each_in->it_type, array->type, BRIDGE_VAR_POD | BRIDGE_VAR_REFERENCE);
                
                ir_value_t *it_ptr = build_lvarptr(builder, array->type, it_var_id);
                ir_value_t *it_idx = build_load(builder, idx_ptr, stmt->source);

                // Update 'it' value
                build_store(builder, build_array_access(builder, array, it_idx, stmt->source), it_ptr, stmt->source);

                // Generate new_block user-defined statements
                bool terminated;
                build_using_basicblock(builder, new_basicblock_id);
                if(ir_gen_stmts(builder, each_in->statements, each_in->statements_length, &terminated)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!terminated){
                    handle_deference_for_variables(builder, &builder->scope->list);
                    build_break(builder, inc_basicblock_id);
                }

                // Finish off initial basic block
                build_using_basicblock(builder, initial_basicblock_id);
                build_break(builder, prep_basicblock_id);

                // Generate jump inc_block
                build_using_basicblock(builder, inc_basicblock_id);

                if(!each_in->is_static && each_in->list){
                    // Call '__defer__' on list value and recompute the list if the each-in loop isn't static

                    // HACK: TODO: Do something better than this
                    ir_value_t *stack_pointer = NULL;
                    ir_value_t *mutable = NULL;
                    
                    if(!phantom_list_value.is_mutable){
                        stack_pointer = build_stack_save(builder);
                        mutable = build_alloc(builder, list_precomputed->type);
                        build_store(builder, list_precomputed, mutable, each_in->list->source);
                    } else {
                        mutable = list_precomputed;
                    }

                    if(handle_single_deference(builder, &phantom_list_value.type, mutable) == ALT_FAILURE){
                        if(stack_pointer) build_stack_restore(builder, stack_pointer);
                        close_scope(builder);
                        return FAILURE;
                    }

                    if(stack_pointer) build_stack_restore(builder, stack_pointer);
                }

                ir_value_t *current_idx = build_load(builder, idx_ptr, stmt->source);
                ir_value_t *ir_one_value = ir_pool_alloc(builder->pool, sizeof(ir_value_t));
                ir_one_value->value_type = VALUE_TYPE_LITERAL;
                ir_type_map_find(builder->type_map, "usize", &(ir_one_value->type));
                ir_one_value->extra = ir_pool_alloc(builder->pool, sizeof(unsigned long long));
                *((unsigned long long*) ir_one_value->extra) = 1;

                // Increment
                ir_value_t *incremented = build_math(builder, INSTRUCTION_ADD, current_idx, ir_one_value, current_idx->type);
                
                // Store
                build_store(builder, incremented, idx_ptr, stmt->source);

                // Jump Prep
                build_break(builder, prep_basicblock_id);

                close_scope(builder);
                build_using_basicblock(builder, end_basicblock_id);

                if(each_in->list){
                    // Call '__defer__' on list value and recompute the list if the each-in loop isn't static

                    // HACK: TODO: Do something better than this
                    ir_value_t *stack_pointer = NULL;
                    ir_value_t *mutable = NULL;
                    
                    if(!phantom_list_value.is_mutable){
                        stack_pointer = build_stack_save(builder);
                        mutable = build_alloc(builder, list_precomputed->type);
                        build_store(builder, list_precomputed, mutable, each_in->list->source);
                    } else {
                        mutable = list_precomputed;
                    }

                    if(handle_single_deference(builder, &phantom_list_value.type, mutable) == ALT_FAILURE){
                        if(stack_pointer) build_stack_restore(builder, stack_pointer);
                        close_scope(builder);
                        return FAILURE;
                    }

                    if(stack_pointer) build_stack_restore(builder, stack_pointer);
                }

                if(each_in->list){
                    // We don't need 'phantom_list_value' anymore, so free its type
                    ast_type_free(&phantom_list_value.type);
                }

                if(each_in->label != NULL) pop_loop_label(builder);

                builder->break_block_id = prev_break_block_id;
                builder->continue_block_id = prev_continue_block_id;
                builder->break_continue_scope = prev_break_continue_scope;
            }
            break;
        case EXPR_REPEAT: {
                ast_expr_repeat_t *repeat = (ast_expr_repeat_t*) stmt;

                length_t prep_basicblock_id = -1;
                length_t new_basicblock_id  = build_basicblock(builder);
                length_t inc_basicblock_id  = build_basicblock(builder);
                length_t end_basicblock_id  = build_basicblock(builder);
                
                if(repeat->label != NULL){
                    push_loop_label(builder, repeat->label, end_basicblock_id, inc_basicblock_id);
                }

                length_t prev_break_block_id = builder->break_block_id;
                length_t prev_continue_block_id = builder->continue_block_id;
                bridge_scope_t *prev_break_continue_scope = builder->break_continue_scope;

                builder->break_block_id = end_basicblock_id;
                builder->continue_block_id = inc_basicblock_id;
                builder->break_continue_scope = builder->scope;

                ast_type_t *idx_ast_type = ast_get_usize(&builder->object->ast);

                ir_type_t *idx_ir_type = ir_builder_usize(builder);
                ir_type_t *idx_ir_type_ptr = ir_builder_usize_ptr(builder);
                
                open_scope(builder);

                // Create 'idx' variable
                length_t idx_var_id = builder->next_var_id;
                add_variable(builder, "idx", idx_ast_type, idx_ir_type, BRIDGE_VAR_POD | BRIDGE_VAR_UNDEF);
                ir_value_t *idx_ptr = build_lvarptr(builder, idx_ir_type_ptr, idx_var_id);

                // Set 'idx' to initial value of zero
                ir_value_t *initial_idx = ir_pool_alloc(builder->pool, sizeof(ir_value_t));
                initial_idx->value_type = VALUE_TYPE_LITERAL;
                initial_idx->type = idx_ir_type;
                initial_idx->extra = ir_pool_alloc(builder->pool, sizeof(unsigned long long));
                *((unsigned long long*) initial_idx->extra) = 0;

                build_store(builder, initial_idx, idx_ptr, stmt->source);

                if(!repeat->is_static){
                    // Use prep block to calculate limit
                    prep_basicblock_id = build_basicblock(builder);
                    build_break(builder, prep_basicblock_id);
                    build_using_basicblock(builder, prep_basicblock_id);
                }

                // Generate length
                ir_value_t *limit;

                if(ir_gen_expr(builder, repeat->limit, &limit, false, &temporary_type)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!ast_types_conform(builder, &limit, &temporary_type, idx_ast_type, CONFORM_MODE_CALCULATION)){
                    char *a_type_str = ast_type_str(&temporary_type);
                    compiler_panicf(builder->compiler, stmt->source, "Received type '%s' when array length should be 'usize'", a_type_str);
                    free(a_type_str);
                    ast_type_free(&temporary_type);
                    close_scope(builder);
                    return FAILURE;
                }

                ast_type_free(&temporary_type);

                if(repeat->is_static){
                    // Use prep block after calculating limit
                    prep_basicblock_id = build_basicblock(builder);
                    build_break(builder, prep_basicblock_id);
                    build_using_basicblock(builder, prep_basicblock_id);
                }

                // Generate (idx < length)
                ir_value_t *idx_value = build_load(builder, idx_ptr, stmt->source);
                ir_value_t *whether_keep_going_value = build_math(builder, INSTRUCTION_ULESSER, idx_value, limit, ir_builder_bool(builder));

                // Generate conditional break
                build_cond_break(builder, whether_keep_going_value, new_basicblock_id, end_basicblock_id);
                build_using_basicblock(builder, new_basicblock_id);

                // Generate new_block user-defined statements
                bool terminated;
                build_using_basicblock(builder, new_basicblock_id);
                if(ir_gen_stmts(builder, repeat->statements, repeat->statements_length, &terminated)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!terminated){
                    handle_deference_for_variables(builder, &builder->scope->list);
                    build_break(builder, inc_basicblock_id);
                }

                // Generate jump inc_block
                build_using_basicblock(builder, inc_basicblock_id);

                ir_value_t *current_idx = build_load(builder, idx_ptr, stmt->source);
                ir_value_t *ir_one_value = ir_pool_alloc(builder->pool, sizeof(ir_value_t));
                ir_one_value->value_type = VALUE_TYPE_LITERAL;
                ir_type_map_find(builder->type_map, "usize", &(ir_one_value->type));
                ir_one_value->extra = ir_pool_alloc(builder->pool, sizeof(unsigned long long));
                *((unsigned long long*) ir_one_value->extra) = 1;

                // Increment
                ir_value_t *incremented = build_math(builder, INSTRUCTION_ADD, current_idx, ir_one_value, current_idx->type);

                // Store
                build_store(builder, incremented, idx_ptr, stmt->source);

                // Jump Prep
                build_break(builder, prep_basicblock_id);

                close_scope(builder);
                build_using_basicblock(builder, end_basicblock_id);

                if(repeat->label != NULL) pop_loop_label(builder);

                builder->break_block_id = prev_break_block_id;
                builder->continue_block_id = prev_continue_block_id;
                builder->break_continue_scope = prev_break_continue_scope;
            }
            break;
        case EXPR_SWITCH: {
                ast_expr_switch_t *switch_expr = (ast_expr_switch_t*) stmt;

                ir_value_t *condition;
                ast_type_t master_ast_type;
                if(ir_gen_expr(builder, switch_expr->value, &condition, false, &master_ast_type)){
                    return FAILURE;
                }

                length_t default_block_id, resume_block_id;
                length_t starting_block_id = builder->current_block_id;

                // STATIC ASSERT: Ensure that all IR integer type kinds are in the expected range
                #define tmp_range(v) (v >= 0x00000002 && v <= 0x00000009)
                #if(!(tmp_range(TYPE_KIND_S8) || tmp_range(TYPE_KIND_S16) || tmp_range(TYPE_KIND_S32) || tmp_range(TYPE_KIND_S64) || \
                    tmp_range(TYPE_KIND_U8) || tmp_range(TYPE_KIND_U16) || tmp_range(TYPE_KIND_U32) || tmp_range(TYPE_KIND_U64)))
                #error "EXPR_SWITCH in ir_gen_stmt.c assumes IR integer type kinds are between 0x00000002 and 0x00000009"
                #endif

                // Use the assumption that IR integer type kinds are in the expected range to check integer-ness
                bool integer_like = tmp_range(condition->type->kind);
                #undef tmp_range

                // Make sure value type is suitable
                if(!integer_like){
                    char *typename = ast_type_str(&master_ast_type);
                    compiler_panicf(builder->compiler, switch_expr->source, "Cannot perform switch on type '%s'", typename);
                    ast_type_free(&master_ast_type);
                    free(typename);
                    return FAILURE;
                }

                ir_value_t **case_values = ir_pool_alloc(builder->pool, sizeof(ir_value_t*) * switch_expr->cases_length);
                length_t *case_block_ids = ir_pool_alloc(builder->pool, sizeof(length_t) * switch_expr->cases_length);
                unsigned long long *uniqueness = malloc(sizeof(unsigned long long) * switch_expr->cases_length);

                // Layout basicblocks for each case
                for(length_t c = 0; c != switch_expr->cases_length; c++){
                    case_block_ids[c] = build_basicblock(builder);
                }
                
                // Create basicblocks for default case and/or resuming control flow
                if(switch_expr->default_statements_length != 0){
                    default_block_id = build_basicblock(builder);
                    resume_block_id = build_basicblock(builder);
                } else {
                    resume_block_id = build_basicblock(builder);
                    default_block_id = resume_block_id;
                }

                // Populate each case
                for(length_t c = 0; c != switch_expr->cases_length; c++){
                    ast_case_t *switch_case = &switch_expr->cases[c];

                    ast_type_t slave_ast_type;
                    if(ir_gen_expr(builder, switch_case->condition, &case_values[c], false, &slave_ast_type)){
                        ast_type_free(&master_ast_type);
                        free(uniqueness);
                        return FAILURE;
                    }

                    if(!ast_types_conform(builder, &case_values[c], &slave_ast_type, &master_ast_type, CONFORM_MODE_CALCULATION)){
                        char *master_typename = ast_type_str(&master_ast_type);
                        char *slave_typename = ast_type_str(&slave_ast_type);
                        compiler_panicf(builder->compiler, switch_case->source, "Case type '%s' is incompatible with expected type '%s'", slave_typename, master_typename);
                        free(master_typename);
                        free(slave_typename);
                        ast_type_free(&slave_ast_type);
                        ast_type_free(&master_ast_type);
                        free(uniqueness);
                        return FAILURE;
                    }

                    ast_type_free(&slave_ast_type);

                    unsigned int value_type = case_values[c]->value_type;
                    if(!VALUE_TYPE_IS_CONSTANT(value_type)){
                        compiler_panicf(builder->compiler, switch_case->source, "Value given to case must be constant");
                        ast_type_free(&master_ast_type);
                        free(uniqueness);
                        return FAILURE;
                    }

                    unsigned long long uniqueness_value = ir_value_uniqueness_value(builder->pool, &case_values[c]);
                    uniqueness[c] = uniqueness_value;

                    for(length_t u = 0; u != c; u++){
                        if(uniqueness_value == uniqueness[u]){
                            compiler_panicf(builder->compiler, switch_expr->cases[u].condition->source, "Non-unique case value");
                            compiler_panicf(builder->compiler, switch_case->condition->source, "Duplicate here");
                            ast_type_free(&master_ast_type);
                            free(uniqueness);
                            return FAILURE;
                        }
                    }
                    
                    length_t prev_fallthrough_block_id = builder->fallthrough_block_id;
                    bridge_scope_t *prev_fallthrough_scope = builder->fallthrough_scope;

                    // For 'fallthrough' statements, go to the next case, if it doesn't exist go to the default/resume case
                    builder->fallthrough_block_id = c + 1 == switch_expr->cases_length ? default_block_id : case_block_ids[c + 1];
                    builder->fallthrough_scope = builder->scope;

                    open_scope(builder);

                    bool case_terminated;
                    build_using_basicblock(builder, case_block_ids[c]);
                    if(ir_gen_stmts(builder, switch_case->statements, switch_case->statements_length, &case_terminated)){
                        ast_type_free(&master_ast_type);
                        close_scope(builder);
                        free(uniqueness);
                        return FAILURE;
                    }

                    if(!case_terminated){
                        handle_deference_for_variables(builder, &builder->scope->list);
                        build_break(builder, resume_block_id);
                    }
                    
                    close_scope(builder);
                    builder->fallthrough_block_id = prev_fallthrough_block_id;
                    builder->fallthrough_scope = prev_fallthrough_scope;
                }

                // If the switch is an exhaustive switch, check whether the supplied values are exhaustive
                if(switch_expr->is_exhaustive && ast_type_is_base(&master_ast_type)){
                    weak_cstr_t enum_name = ((ast_elem_base_t*) master_ast_type.elements[0])->base;
                    
                    if(exhaustive_switch_check(builder, enum_name, switch_expr->source, uniqueness, switch_expr->cases_length)){
                        ast_type_free(&master_ast_type);
                        free(uniqueness);
                        return FAILURE;
                    }
                }

                free(uniqueness);

                // Fill in statements for default block
                if(default_block_id != resume_block_id){
                    open_scope(builder);

                    bool case_terminated;
                    build_using_basicblock(builder, default_block_id);
                    if(ir_gen_stmts(builder, switch_expr->default_statements, switch_expr->default_statements_length, &case_terminated)){
                        ast_type_free(&master_ast_type);
                        close_scope(builder);
                        return FAILURE;
                    }

                    if(!case_terminated){
                        handle_deference_for_variables(builder, &builder->scope->list);
                        build_break(builder, resume_block_id);
                    }
                    
                    close_scope(builder);
                }

                ast_type_free(&master_ast_type);

                build_using_basicblock(builder, starting_block_id);
                built_instr = build_instruction(builder, sizeof(ir_instr_switch_t));
                ((ir_instr_switch_t*) built_instr)->id = INSTRUCTION_SWITCH;
                ((ir_instr_switch_t*) built_instr)->result_type = condition->type;
                ((ir_instr_switch_t*) built_instr)->condition = condition;
                ((ir_instr_switch_t*) built_instr)->cases_length = switch_expr->cases_length;
                ((ir_instr_switch_t*) built_instr)->case_values = case_values;
                ((ir_instr_switch_t*) built_instr)->case_block_ids = case_block_ids;
                ((ir_instr_switch_t*) built_instr)->default_block_id = default_block_id;
                ((ir_instr_switch_t*) built_instr)->resume_block_id = resume_block_id;
                build_using_basicblock(builder, resume_block_id);
            }
            break;
        case EXPR_VA_START: case EXPR_VA_END: {
                ast_expr_unary_t *va_expr = (ast_expr_unary_t*) stmt;
                if(ir_gen_expr(builder, va_expr->value, &expression_value, true, &temporary_type)) return FAILURE;
                
                if(!ast_type_is_base_of(&temporary_type, "va_list")){
                    char *t = ast_type_str(&temporary_type);
                    compiler_panicf(builder->compiler, va_expr->source, "Can't pass non-'va_list' type '%s' to va_%s", t, stmt->id == EXPR_VA_START ? "start" : "end");
                    ast_type_free(&temporary_type);
                    free(t);
                    return FAILURE;
                }

                if(!expr_is_mutable(va_expr->value)){
                    compiler_panicf(builder->compiler, va_expr->source, "Value passed to va_%s must be mutable", stmt->id == EXPR_VA_START ? "start" : "end");
                    return FAILURE;
                }

                // Cast from *va_list to *s8
                expression_value = build_bitcast(builder, expression_value, builder->ptr_type);

                built_instr = build_instruction(builder, sizeof(ir_instr_unary_t));
                ((ir_instr_unary_t*) built_instr)->id = stmt->id == EXPR_VA_START ? INSTRUCTION_VA_START : INSTRUCTION_VA_END;
                ((ir_instr_unary_t*) built_instr)->result_type = NULL;
                ((ir_instr_unary_t*) built_instr)->value = expression_value;

                ast_type_free(&temporary_type);
            }
            break;
        case EXPR_VA_COPY: {
                ast_expr_va_copy_t *va_copy_expr = (ast_expr_va_copy_t*) stmt;

                ir_value_t *dest_value;
                if(ir_gen_expr(builder, va_copy_expr->dest_value, &dest_value, true, &temporary_type)) return FAILURE;
                
                if(!ast_type_is_base_of(&temporary_type, "va_list")){
                    char *t = ast_type_str(&temporary_type);
                    compiler_panicf(builder->compiler, va_copy_expr->dest_value->source, "Can't pass non-'va_list' type '%s' to va_copy", t);
                    ast_type_free(&temporary_type);
                    free(t);
                    return FAILURE;
                }

                if(!expr_is_mutable(va_copy_expr->dest_value)){
                    compiler_panicf(builder->compiler, va_copy_expr->dest_value->source, "Value passed to va_copy must be mutable");
                    return FAILURE;
                }

                ast_type_free(&temporary_type);

                ir_value_t *src_value;
                if(ir_gen_expr(builder, va_copy_expr->src_value, &src_value, true, &temporary_type)) return FAILURE;
                
                if(!ast_type_is_base_of(&temporary_type, "va_list")){
                    char *t = ast_type_str(&temporary_type);
                    compiler_panicf(builder->compiler, va_copy_expr->src_value->source, "Can't pass non-'va_list' type '%s' to va_copy", t);
                    ast_type_free(&temporary_type);
                    free(t);
                    return FAILURE;
                }

                if(!expr_is_mutable(va_copy_expr->dest_value)){
                    compiler_panicf(builder->compiler, va_copy_expr->src_value->source, "Value passed to va_copy must be mutable");
                    return FAILURE;
                }

                // Cast from *va_list to *s8
                dest_value = build_bitcast(builder, dest_value, builder->ptr_type);
                src_value   = build_bitcast(builder, src_value, builder->ptr_type);

                built_instr = build_instruction(builder, sizeof(ir_instr_va_copy_t));
                ((ir_instr_va_copy_t*) built_instr)->id = INSTRUCTION_VA_COPY;
                ((ir_instr_va_copy_t*) built_instr)->result_type = NULL;
                ((ir_instr_va_copy_t*) built_instr)->dest_value = dest_value;
                ((ir_instr_va_copy_t*) built_instr)->src_value = src_value;

                ast_type_free(&temporary_type);
            }
            break;
        case EXPR_FOR: {
                ast_expr_for_t *for_loop = (ast_expr_for_t*) stmt;
                length_t prep_basicblock_id = -1;
                length_t new_basicblock_id  = build_basicblock(builder);
                length_t end_basicblock_id  = build_basicblock(builder);

                if(for_loop->label != NULL){
                    push_loop_label(builder, for_loop->label, end_basicblock_id, prep_basicblock_id);
                }

                length_t prev_break_block_id = builder->break_block_id;
                length_t prev_continue_block_id = builder->continue_block_id;
                bridge_scope_t *prev_break_continue_scope = builder->break_continue_scope;

                builder->break_block_id = end_basicblock_id;
                builder->continue_block_id = prep_basicblock_id;
                builder->break_continue_scope = builder->scope;

                open_scope(builder);

                // Do 'before' statements
                bool terminated;
                if(ir_gen_stmts(builder, for_loop->before.statements, for_loop->before.length, &terminated)){
                    close_scope(builder);
                    return FAILURE;
                }

                // Don't allow 'return'/'continue'/'break' in 'before' statements
                if(terminated){
                    compiler_panic(builder->compiler, for_loop->before.statements[0]->source, "The 'before' statements of a 'for' loop cannot contain a terminator");
                    close_scope(builder);
                    return FAILURE;
                }

                // Use prep block to calculate condition
                prep_basicblock_id = build_basicblock(builder);
                build_break(builder, prep_basicblock_id);
                build_using_basicblock(builder, prep_basicblock_id);

                // Generate condition
                ir_value_t *condition_value;

                if(for_loop->condition){
                    if(ir_gen_expr(builder, for_loop->condition, &condition_value, false, &temporary_type)) return FAILURE;
                } else {
                    ast_type_make_base(&temporary_type, "bool");
                    condition_value = build_bool(builder->pool, true);
                }

                // Create static bool type for comparison with
                ast_elem_base_t bool_base;
                bool_base.id = AST_ELEM_BASE;
                bool_base.source = NULL_SOURCE;
                bool_base.source.object_index = builder->object->index;
                bool_base.base = "bool";
                ast_elem_t *bool_type_elem = (ast_elem_t*) &bool_base;
                ast_type_t bool_type;
                bool_type.elements = &bool_type_elem;
                bool_type.elements_length = 1;
                bool_type.source = NULL_SOURCE;
                bool_type.source.object_index = builder->object->index;

                if(!ast_types_conform(builder, &condition_value, &temporary_type, &bool_type, CONFORM_MODE_CALCULATION)){
                    char *a_type_str = ast_type_str(&temporary_type);
                    char *b_type_str = ast_type_str(&bool_type);
                    compiler_panicf(builder->compiler, stmt->source, "Received type '%s' when conditional expects type '%s'", a_type_str, b_type_str);
                    free(a_type_str);
                    free(b_type_str);
                    ast_type_free(&temporary_type);
                    return FAILURE;
                }

                // Dispose of temporary AST type of condition
                ast_type_free(&temporary_type);

                // Generate conditional break
                build_cond_break(builder, condition_value, new_basicblock_id, end_basicblock_id);
                build_using_basicblock(builder, new_basicblock_id);

                // Generate new_block user-defined statements
                if(ir_gen_stmts(builder, for_loop->statements.statements, for_loop->statements.length, &terminated)){
                    close_scope(builder);
                    return FAILURE;
                }

                if(!terminated){
                    handle_deference_for_variables(builder, &builder->scope->list);
                    build_break(builder, prep_basicblock_id);
                }

                close_scope(builder);
                build_using_basicblock(builder, end_basicblock_id);

                if(for_loop->label != NULL) pop_loop_label(builder);

                builder->break_block_id = prev_break_block_id;
                builder->continue_block_id = prev_continue_block_id;
                builder->break_continue_scope = prev_break_continue_scope;
            }
            break;
        case EXPR_DECLARE_CONSTANT:
            // This statement was handled during the inference stage
            break;
        case EXPR_LLVM_ASM: {
                ast_expr_llvm_asm_t *asm_expr = (ast_expr_llvm_asm_t*) stmt;
                ir_value_t **args = ir_pool_alloc(builder->pool, sizeof(ir_value_t*) * asm_expr->arity);

                for(length_t i = 0; i != asm_expr->arity; i++){
                    if(ir_gen_expr(builder, asm_expr->args[i], &args[i], false, NULL)){
                        return FAILURE;
                    }
                }

                build_llvm_asm(builder, asm_expr->is_intel, asm_expr->assembly,
                    asm_expr->constraints, args, asm_expr->arity,
                    asm_expr->has_side_effects, asm_expr->is_stack_align);
            }
            break;
        default:
            compiler_panic(builder->compiler, stmt->source, "INTERNAL ERROR: Unimplemented statement in ir_gen_stmts()");
            return FAILURE;
        }
    }

    return SUCCESS;
}

errorcode_t ir_gen_stmt_return(ir_builder_t *builder, ast_expr_return_t *stmt, bool *out_is_terminated){
    ast_t *ast = &builder->object->ast;

    ast_type_t return_type;
    bool is_in_main_function;
    bool is_in_pass_function;
    bool is_in_defer_function;
    bool autogen_enabled;
    
    // 'ast_func' is only guaranteed to be valid within this scope.
    // Afterwards, it may or may not be shifted around in memory.
    // Because of this, no pointers should be taken relative to 'ast_func'.
    {
        ast_func_t *ast_func = &ast->funcs[builder->ast_func_id];

        return_type = ast_func->return_type;
        is_in_main_function  = ast_func->traits & AST_FUNC_MAIN;
        is_in_pass_function  = ast_func->traits & AST_FUNC_PASS;
        is_in_defer_function = ast_func->traits & AST_FUNC_DEFER;
        autogen_enabled      = ast_func->traits & AST_FUNC_AUTOGEN;
    }

    ir_value_t *ir_value_to_be_returned = NULL;
    bool returns_void = ast_type_is_void(&return_type);

    if(stmt->value != NULL){
        if(returns_void){
            compiler_panicf(builder->compiler, stmt->source, "Can't return a value from function that returns void");
            return FAILURE;
        }

        ast_type_t value_return_type;

        // Generate instructions to calculate return value
        if(ir_gen_expr(builder, stmt->value, &ir_value_to_be_returned, false, &value_return_type)) return FAILURE;

        // Conform return value to expected return type
        if(!ast_types_conform(builder, &ir_value_to_be_returned, &value_return_type, &return_type, CONFORM_MODE_CALCULATION)){
            char *a_type_str = ast_type_str(&value_return_type);
            char *b_type_str = ast_type_str(&return_type);
            compiler_panicf(builder->compiler, stmt->source, "Attempting to return type '%s' when function expects type '%s'", a_type_str, b_type_str);
            free(a_type_str);
            free(b_type_str);
            ast_type_free(&value_return_type);
            return FAILURE;
        }

        ast_type_free(&value_return_type);
    } else if(is_in_main_function && returns_void){
        // Return 0 if in main function and it returns void
        ir_value_to_be_returned = build_literal_int(builder->pool, 0);
    } else if(!returns_void){
        // This function expects a value to returned, not void
        char *a_type_str = ast_type_str(&return_type);
        compiler_panicf(builder->compiler, stmt->source, "Attempting to return void when function expects type '%s'", a_type_str);
        free(a_type_str);
        return FAILURE;
    } else {
        // Return void
        ir_value_to_be_returned = NULL;
    }

    // Handle deferred statements, making sure to prohibit terminating statements
    {
        bool illegally_terminated;

        if(ir_gen_stmts(builder, stmt->last_minute.statements, stmt->last_minute.length, &illegally_terminated))
            return FAILURE;

        if(illegally_terminated){
            compiler_panicf(builder->compiler, stmt->source, "Cannot expand a previously deferred terminating statement");
            return FAILURE;
        }
    }

    // Make '__defer__()' calls for variables running out of scope
    {
        bridge_scope_t *visit_scope = builder->scope;

        do {
            handle_deference_for_variables(builder, &visit_scope->list);
            visit_scope = visit_scope->parent;
        } while(visit_scope != NULL);
    }

    // Make '__defer__()' calls for global variables and (anonymous) static variables running out of scope
    if(is_in_main_function){
        handle_deference_for_globals(builder);
        build_deinit_svars(builder);
    }

    // If auto-generation is enabled and this function is eligible,
    // then attempt to do so
    if(autogen_enabled){
        if(
            (is_in_pass_function  && handle_children_pass_root(builder, true)) ||
            (is_in_defer_function && handle_children_deference(builder))
        ) return FAILURE;
    }

    // Return the determined value
    build_return(builder, ir_value_to_be_returned);
    
    // The 'return' statement is always a terminating statement
    if(out_is_terminated) *out_is_terminated = true;

    return SUCCESS;
}

errorcode_t ir_gen_stmt_call_like(ir_builder_t *builder, ast_expr_t *call_like_stmt){
    ast_type_t dropped_type;
    ir_value_t *dropped_value;

    // Handle call-like statements as expressions
    if(ir_gen_expr(builder, call_like_stmt, &dropped_value, true, &dropped_type)) return FAILURE;

    weak_cstr_t base_name = NULL;

    if(ast_type_is_base(&dropped_type)){
        base_name = ((ast_elem_base_t*) dropped_type.elements[0])->base;
    } else if(ast_type_is_generic_base(&dropped_type)){
        base_name = ((ast_elem_generic_base_t*) dropped_type.elements[0])->name;
    }
    
    // Handle dropped values from call expressions
    if(base_name && !typename_is_entended_builtin_type(base_name)){
        // Temporarily allocate space on the stack to store the dropped value
        ir_value_t *stack_pointer = build_stack_save(builder);
        ir_value_t *temporary_mutable = build_alloc(builder, dropped_value->type);
        build_store(builder, dropped_value, temporary_mutable, call_like_stmt->source);

        // Properly clean up the dropped value
        if(handle_single_deference(builder, &dropped_type, temporary_mutable) == ALT_FAILURE){
            ast_type_free(&dropped_type);
            return FAILURE;
        }

        build_stack_restore(builder, stack_pointer);
    }

    ast_type_free(&dropped_type);
    return SUCCESS;
}

errorcode_t ir_gen_stmt_declare(ir_builder_t *builder, ast_expr_declare_t *stmt){
    ast_type_t initial_value_type;

    // Search for existing variable with the same name
    if(bridge_scope_var_already_in_list(builder->scope, stmt->name)){
        compiler_panicf(builder->compiler, stmt->source, "Variable '%s' already declared", stmt->name);
        return FAILURE;
    }

    ir_type_t *ir_decl_type = ir_pool_alloc(builder->pool, sizeof(ir_type_t));
    if(ir_gen_resolve_type(builder->compiler, builder->object, &stmt->type, &ir_decl_type)) return FAILURE;

    // Create variable traits
    trait_t variable_traits = stmt->is_pod ? BRIDGE_VAR_POD : TRAIT_NONE;

    ir_builder_t *init_builder;
    ir_builder_t *initialization_builder = builder->object->ir_module.init_builder;

    if(stmt->is_static){
        variable_traits |= BRIDGE_VAR_STATIC;

        if(!builder->object->ir_module.common.has_main){
            errorprintf("Cannot use static variables without a main function\n");
            return FAILURE;
        }

        init_builder = initialization_builder;
        initialization_builder->scope = malloc(sizeof(bridge_scope_t));
        bridge_scope_init(initialization_builder->scope, NULL);
    } else {
        init_builder = builder;
    }

    // TODO: Clean up this messy code
    if(stmt->value != NULL){
        // Regular declare statement initial assign value
        ir_value_t *initial;
        ir_type_t *var_pointer_type = ir_type_pointer_to(builder->pool, ir_decl_type);

        if(ir_gen_expr(init_builder, stmt->value, &initial, false, &initial_value_type)) return FAILURE;

        if(!ast_types_conform(init_builder, &initial, &initial_value_type, &stmt->type, CONFORM_MODE_ASSIGNING)){
            char *a_type_str = ast_type_str(&initial_value_type);
            char *b_type_str = ast_type_str(&stmt->type);
            compiler_panicf(builder->compiler, stmt->source, "Incompatible types '%s' and '%s'", a_type_str, b_type_str);
            free(a_type_str);
            free(b_type_str);
            ast_type_free(&initial_value_type);
            return FAILURE;
        }

        ir_value_t *destination;

        if(stmt->is_static){
            destination = build_svarptr(init_builder, var_pointer_type, builder->object->ir_module.common.next_static_variable_id);
        } else {
            destination = build_lvarptr(init_builder, var_pointer_type, builder->next_var_id);
        }

        add_variable(builder, stmt->name, &stmt->type, ir_decl_type, variable_traits);

        if(stmt->is_assign_pod || !handle_assign_management(init_builder, initial, &initial_value_type, destination, &stmt->type, true)){
            build_store(init_builder, initial, destination, stmt->source);
        }

        ast_type_free(&initial_value_type);
    } else if(stmt->id == EXPR_DECLAREUNDEF && !(builder->compiler->traits & COMPILER_NO_UNDEF)){
        // Mark the variable as undefined memory so it isn't auto-initialized later on

        add_variable(builder, stmt->name, &stmt->type, ir_decl_type, variable_traits | BRIDGE_VAR_UNDEF);
    } else /* plain DECLARE or --no-undef DECLAREUNDEF */ {
        // Variable declaration without default value
        add_variable(builder, stmt->name, &stmt->type, ir_decl_type, variable_traits);

        ir_basicblock_new_instructions(init_builder->current_block, 1);

        ir_value_t *destination;
        ir_type_t *var_pointer_type = ir_type_pointer_to(builder->pool, ir_decl_type);

        if(stmt->is_static){
            destination = build_svarptr(init_builder, var_pointer_type, builder->object->ir_module.common.next_static_variable_id - 1);
        } else {
            destination = build_lvarptr(init_builder, var_pointer_type, builder->next_var_id - 1);
        }

        build_zeroinit(init_builder, destination);
    }

    if(init_builder == initialization_builder){
        bridge_scope_free(initialization_builder->scope);
        free(initialization_builder->scope);
    }

    return SUCCESS;
}

errorcode_t exhaustive_switch_check(ir_builder_t *builder, weak_cstr_t enum_name, source_t switch_source, unsigned long long uniqueness_values[], length_t uniqueness_values_length){
    ast_t *ast = &builder->object->ast;
    maybe_index_t enum_index = ast_find_enum(ast->enums, ast->enums_length, enum_name);
    if(enum_index == -1) return SUCCESS;

    // Assumes regular 0..n enum
    ast_enum_t *enum_definition = &ast->enums[enum_index];
    length_t n = enum_definition->length;

    // Don't check enums that have more than 512 elements
    if(n > 512){
        if(uniqueness_values_length < n){
            compiler_panic(builder->compiler, switch_source, "Exhaustive switch with more than 512 elements is missing cases");
            return FAILURE;
        } else if(uniqueness_values_length > n){
            compiler_panic(builder->compiler, switch_source, "Exhaustive switch with more than 512 elements has extraneous cases");
            return FAILURE;
        }

        return SUCCESS;
    }
    
    bool covered[n];
    memset(covered, 0, sizeof(covered));

    for(length_t i = 0; i < uniqueness_values_length; i++){
        if(uniqueness_values[i] >= n){
            compiler_panic(builder->compiler, switch_source, "Exhaustive switch got out of bounds case value");
            return FAILURE;
        }

        covered[uniqueness_values[i]] = true;
    }

    bool is_missing_case = false;
    for(length_t i = 0; i < n; i++) if(!covered[i]){
        // If missing case
        
        if(!is_missing_case){
            is_missing_case = true;

            compiler_panic(builder->compiler, switch_source, "Not all cases covered in exhaustive switch statement");
            printf("\nMissing cases:\n");
        }
        
        printf("    case %s::%s\n", enum_name, enum_definition->kinds[i]);
    }

    return is_missing_case ? FAILURE : SUCCESS;
}
