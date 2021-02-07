
#include "DRVR/object.h"

void object_init_ast(object_t *object, unsigned int cross_compile_for){
    ast_init(&object->ast, cross_compile_for);
    object->compilation_stage = COMPILATION_STAGE_AST;
}

ast_composite_t *object_composite_find(ast_t *override_main_ast, object_t *object, tmpbuf_t *tmpbuf, const char *name, bool *out_requires_namespace){
    // If 'override_main_ast' is NULL, then use 'object->ast'
    if(override_main_ast == NULL) override_main_ast = &object->ast;

    ast_composite_t *found;
    weak_cstr_t try_name;
    
    // First, try to locate in the current namespace
    if(object->current_namespace){
        try_name = tmpbuf_quick_concat3(tmpbuf, object->current_namespace, "\\", name);
        found = ast_composite_find_exact(override_main_ast, try_name);
        if(found){
            if(out_requires_namespace) *out_requires_namespace = true;
            return found;
        }
    }

    // Second, try to locate in 'used' namespaces
    for(length_t i = 0; i != object->using_namespaces_length; i++){
        try_name = tmpbuf_quick_concat3(tmpbuf, object->using_namespaces[i].cstr, "\\", name);
        found = ast_composite_find_exact(override_main_ast, try_name);
        if(found){
            if(out_requires_namespace) *out_requires_namespace = true;
            return found;
        }
    }

    // Lastly, try to locate in global namespace
    if(out_requires_namespace) *out_requires_namespace = false;
    return ast_composite_find_exact(override_main_ast, name);
}

ast_polymorphic_composite_t *object_polymorphic_composite_find(ast_t *override_main_ast, object_t *object, tmpbuf_t *tmpbuf, const char *name, bool *out_requires_namespace){
    // If 'override_main_ast' is NULL, then use 'object->ast'
    if(override_main_ast == NULL) override_main_ast = &object->ast;

    ast_polymorphic_composite_t *found;
    weak_cstr_t try_name;
    
    // First, try to locate in the current namespace
    if(object->current_namespace){
        try_name = tmpbuf_quick_concat3(tmpbuf, object->current_namespace, "\\", name);
        found = ast_polymorphic_composite_find_exact(override_main_ast, try_name);
        if(found){
            if(out_requires_namespace) *out_requires_namespace = true;
            return found;
        }
    }

    // Second, try to locate in 'used' namespaces
    for(length_t i = 0; i != object->using_namespaces_length; i++){
        try_name = tmpbuf_quick_concat3(tmpbuf, object->using_namespaces[i].cstr, "\\", name);
        found = ast_polymorphic_composite_find_exact(override_main_ast, try_name);
        if(found){
            if(out_requires_namespace) *out_requires_namespace = true;
            return found;
        }
    }

    // Lastly, try to locate in global namespace
    if(out_requires_namespace) *out_requires_namespace = false;
    return ast_polymorphic_composite_find_exact(override_main_ast, name);
}

successful_t ir_type_map_loose_find(object_t *namespace_object, ir_type_map_t *type_map, weak_cstr_t base, ir_type_t **resolved_type){
    if(ir_type_map_find(type_map, base, resolved_type)) return true;
    
    tmpbuf_t tmpbuf;
    tmpbuf_init(&tmpbuf);

    for(length_t i = 0; i != namespace_object->using_namespaces_length; i++){
        weak_cstr_t namespace = namespace_object->using_namespaces[i].cstr;
        weak_cstr_t namespaced = tmpbuf_quick_concat3(&tmpbuf, namespace, "\\", base);

        if(ir_type_map_find(type_map, namespaced, resolved_type)){
            tmpbuf_free(&tmpbuf);
            return true;
        }
    }

    tmpbuf_free(&tmpbuf);
    return false;
}
