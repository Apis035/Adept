
#include "UTIL/color.h"
#include "UTIL/filename.h"
#include "PARSE/parse_util.h"

int parse_ignore_newlines(parse_ctx_t *ctx, const char *error_message){
    length_t *i = ctx->i;
    token_t *tokens = ctx->tokenlist->tokens;
    length_t length = ctx->tokenlist->length;

    while(tokens[*i].id == TOKEN_NEWLINE) if(length == (*i)++){
        compiler_panic(ctx->compiler, ctx->tokenlist->sources[*i - 1], error_message);
        return 1;
    }

    return 0;
}

void parse_panic_token(parse_ctx_t *ctx, source_t source, unsigned int token_id, const char *message){
    // Expects from 'ctx': compiler, object
    // Expects 'message' to have a '%s' to display the token name

    int line, column;
    length_t message_length = strlen(message);
    char *format = malloc(message_length + 11);

    if(ctx->object->traits & OBJECT_PACKAGE){
        memcpy(format, "%s: ", 4);
        memcpy(&format[4], message, message_length);
        memcpy(&format[4 + message_length], "!\n", 3);
        redprintf(format, filename_name_const(ctx->object->filename), global_token_name_table[token_id]);
    } else {
        lex_get_location(ctx->object->buffer, source.index, &line, &column);
        memcpy(format, "%s:%d:%d: ", 10);
        memcpy(&format[10], message, message_length);
        memcpy(&format[10 + message_length], "!\n", 3);
        redprintf(format, filename_name_const(ctx->object->filename), line, column, global_token_name_table[token_id]);
    }

    free(format);
    compiler_print_source(ctx->compiler, line, column, source);
}