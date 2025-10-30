#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jsasta_compiler.h"
#include "logger.h"

// Helper macro to create AST nodes with current token location
#define AST_NODE(parser, type) ast_create_with_loc(type, (SourceLocation){ \
    .filename = (parser)->filename, \
    .line = (parser)->current_token->line, \
    .column = (parser)->current_token->column \
})

static void parser_advance(Parser* parser) {
    if (parser->current_token) {
        token_free(parser->current_token);
    }
    parser->current_token = lexer_next_token(parser->lexer);
}

static bool parser_match(Parser* parser, TokenType type) {
    return parser->current_token->type == type;
}

static bool parser_expect(Parser* parser, TokenType type) {
    if (!parser_match(parser, type)) {
        SourceLocation loc = {
            .filename = parser->filename,
            .line = parser->current_token->line,
            .column = parser->current_token->column
        };
        log_error_at(&loc, "Expected token type %d, got %d", type, parser->current_token->type);
        return false;
    }
    parser_advance(parser);
    return true;
}

// Helper to parse optional type annotation (: type or : { prop: type, ... })
static TypeInfo* parse_type_annotation(Parser* parser) {
    if (!parser_match(parser, TOKEN_COLON)) {
        return NULL;  // No type annotation
    }

    parser_advance(parser);  // consume ':'

    // Check if it's a reference type (starts with 'ref')
    bool is_ref = false;
    if (parser_match(parser, TOKEN_REF)) {
        is_ref = true;
        parser_advance(parser);  // consume 'ref'
    }

    // Check if it's an object type (starts with '{')
    if (parser_match(parser, TOKEN_LBRACE)) {
        parser_advance(parser);  // consume '{'

        TypeInfo* type_info = type_info_create(TYPE_KIND_OBJECT, NULL);

        // Parse property types
        int capacity = 4;
        type_info->data.object.property_names = (char**)malloc(sizeof(char*) * capacity);
        type_info->data.object.property_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * capacity);
        type_info->data.object.property_count = 0;

        if (!parser_match(parser, TOKEN_RBRACE)) {
            do {
                if (type_info->data.object.property_count >= capacity) {
                    capacity *= 2;
                    type_info->data.object.property_names = (char**)realloc(type_info->data.object.property_names, sizeof(char*) * capacity);
                    type_info->data.object.property_types = (TypeInfo**)realloc(type_info->data.object.property_types, sizeof(TypeInfo*) * capacity);
                }

                // Parse property name
                if (!parser_match(parser, TOKEN_IDENTIFIER)) {
                    log_error("Expected property name in object type");
                    type_info_free(type_info);
                    return NULL;
                }
                type_info->data.object.property_names[type_info->data.object.property_count] = strdup(parser->current_token->value);
                parser_advance(parser);

                // Parse property type recursively
                type_info->data.object.property_types[type_info->data.object.property_count] = parse_type_annotation(parser);
                if (!type_info->data.object.property_types[type_info->data.object.property_count]) {
                    log_error("Expected type annotation for property");
                    type_info_free(type_info);
                    return NULL;
                }

                type_info->data.object.property_count++;

                // Check for comma
                if (parser_match(parser, TOKEN_COMMA)) {
                    parser_advance(parser);
                    if (parser_match(parser, TOKEN_RBRACE)) {
                        break;  // Trailing comma
                    }
                } else {
                    break;
                }
            } while (true);
        }

        if (!parser_match(parser, TOKEN_RBRACE)) {
            log_error("Expected '}' after object type");
            type_info_free(type_info);
            return NULL;
        }
        parser_advance(parser);  // consume '}'

        return type_info;
    }

    // Parse primitive type - check for type tokens first
    TypeInfo* type_info = NULL;
    
    // Check for integer type tokens
    if (parser_match(parser, TOKEN_I8)) {
        type_info = Type_I8;
    } else if (parser_match(parser, TOKEN_I16)) {
        type_info = Type_I16;
    } else if (parser_match(parser, TOKEN_I32)) {
        type_info = Type_I32;
    } else if (parser_match(parser, TOKEN_I64)) {
        type_info = Type_I64;
    } else if (parser_match(parser, TOKEN_U8)) {
        type_info = Type_U8;
    } else if (parser_match(parser, TOKEN_U16)) {
        type_info = Type_U16;
    } else if (parser_match(parser, TOKEN_U32)) {
        type_info = Type_U32;
    } else if (parser_match(parser, TOKEN_U64)) {
        type_info = Type_U64;
    } else if (parser_match(parser, TOKEN_INT)) {
        type_info = Type_Int;
    } else if (parser_match(parser, TOKEN_IDENTIFIER)) {
        // Parse by identifier name (for backward compatibility and other types)
        const char* type_name = parser->current_token->value;

        if (strcmp(type_name, "int") == 0) {
            type_info = Type_Int;
        } else if (strcmp(type_name, "double") == 0) {
            type_info = Type_Double;
        } else if (strcmp(type_name, "string") == 0) {
            type_info = Type_String;
        } else if (strcmp(type_name, "bool") == 0) {
            type_info = Type_Bool;
        } else if (strcmp(type_name, "void") == 0) {
            type_info = Type_Void;
        } else if (strcmp(type_name, "usize") == 0) {
            type_info = Type_Usize;
        } else if (strcmp(type_name, "nint") == 0) {
            type_info = Type_Nint;
        } else if (strcmp(type_name, "uint") == 0) {
            type_info = Type_Uint;
        }
    }
    
    if (!type_info) {
        // Try identifier for struct types
        if (!parser_match(parser, TOKEN_IDENTIFIER)) {
            log_error("Expected type name after ':'");
            return NULL;
        }
        const char* type_name = parser->current_token->value;
        // Try to look up struct type from TypeContext
        if (parser->type_ctx) {
            type_info = type_context_find_struct_type(parser->type_ctx, type_name);
            if (!type_info) {
                log_error_at(SRC_LOC(parser->filename, parser->current_token->line, parser->current_token->column), 
                            "Unknown type '%s'", type_name);
                type_info = Type_Unknown;
            }
        } else {
            log_error_at(SRC_LOC(parser->filename, parser->current_token->line, parser->current_token->column), 
                        "Unknown type '%s'", type_name);
            type_info = Type_Unknown;
        }
    }

    parser_advance(parser);  // consume type name

    // Check for array syntax [size] - but don't consume it here
    // The caller (parse_struct_declaration) will handle the array size
    // We just return the element type
    
    // If this is a ref type, wrap it
    if (is_ref) {
        TypeInfo* ref_type = type_info_create(TYPE_KIND_REF, NULL);
        ref_type->data.ref.target_type = type_info;
        ref_type->data.ref.is_mutable = true;  // Default to mutable
        
        // Generate type name like "ref<termios>"
        char type_name[256];
        snprintf(type_name, sizeof(type_name), "ref<%s>", type_info->type_name ? type_info->type_name : "?");
        ref_type->type_name = strdup(type_name);
        
        return ref_type;
    }
    
    // Return type info (primitive or struct)
    return type_info;
}

Parser* parser_create(const char* source, const char* filename, TypeContext* type_ctx) {
    Parser* parser = (Parser*)malloc(sizeof(Parser));
    parser->lexer = lexer_create(source);
    parser->current_token = NULL;
    parser->filename = filename;
    parser->type_ctx = type_ctx;
    parser_advance(parser);
    return parser;
}

void parser_free(Parser* parser) {
    if (parser->current_token) {
        token_free(parser->current_token);
    }
    lexer_free(parser->lexer);
    free(parser);
}

// Forward declarations
static ASTNode* parse_statement(Parser* parser);
static ASTNode* parse_expression(Parser* parser);

static ASTNode* parse_primary(Parser* parser) {
    ASTNode* node = NULL;

    if (parser_match(parser, TOKEN_NUMBER)) {
        node = AST_NODE(parser, AST_NUMBER);
        const char* value_str = parser->current_token->value;
        
        // Check for integer type suffix (i8, i16, i32, i64, u8, u16, u32, u64)
        TypeInfo* explicit_type = NULL;
        size_t len = strlen(value_str);
        
        // Check for 2-character suffixes (i8, u8)
        if (len >= 2) {
            const char* suffix = &value_str[len - 2];
            if (strcmp(suffix, "i8") == 0) {
                explicit_type = Type_I8;
                len -= 2;
            } else if (strcmp(suffix, "u8") == 0) {
                explicit_type = Type_U8;
                len -= 2;
            }
        }
        
        // Check for 3-character suffixes (i16, i32, i64, u16, u32, u64)
        if (!explicit_type && len >= 3) {
            const char* suffix = &value_str[len - 3];
            if (strcmp(suffix, "i16") == 0) {
                explicit_type = Type_I16;
                len -= 3;
            } else if (strcmp(suffix, "i32") == 0) {
                explicit_type = Type_I32;
                len -= 3;
            } else if (strcmp(suffix, "i64") == 0) {
                explicit_type = Type_I64;
                len -= 3;
            } else if (strcmp(suffix, "u16") == 0) {
                explicit_type = Type_U16;
                len -= 3;
            } else if (strcmp(suffix, "u32") == 0) {
                explicit_type = Type_U32;
                len -= 3;
            } else if (strcmp(suffix, "u64") == 0) {
                explicit_type = Type_U64;
                len -= 3;
            }
        }
        
        // Parse the numeric value (without suffix)
        if (explicit_type) {
            // Create temporary string without suffix
            char* num_str = strndup(value_str, len);
            node->number.value = atof(num_str);
            free(num_str);
            node->type_info = explicit_type;
        } else {
            // No suffix - determine type automatically
            node->number.value = atof(value_str);
            
            // Check if it's a floating point number
            if (strchr(value_str, '.') || strchr(value_str, 'e') || strchr(value_str, 'E')) {
                node->type_info = Type_Double;
            } else {
                // Integer literal without suffix defaults to i32
                node->type_info = Type_I32;
            }
        }
        
        parser_advance(parser);
    } else if (parser_match(parser, TOKEN_STRING)) {
        node = AST_NODE(parser, AST_STRING);
        node->string.value = strdup(parser->current_token->value);
        node->type_info = Type_String;
        parser_advance(parser);
    } else if (parser_match(parser, TOKEN_TRUE) || parser_match(parser, TOKEN_FALSE)) {
        node = AST_NODE(parser, AST_BOOLEAN);
        node->boolean.value = parser_match(parser, TOKEN_TRUE);
        node->type_info = Type_Bool;
        parser_advance(parser);
    } else if (parser_match(parser, TOKEN_IDENTIFIER)) {
        node = AST_NODE(parser, AST_IDENTIFIER);
        node->identifier.name = strdup(parser->current_token->value);
        parser_advance(parser);
    } else if (parser_match(parser, TOKEN_LPAREN)) {
        parser_advance(parser);
        node = parse_expression(parser);
        parser_expect(parser, TOKEN_RPAREN);
    } else if (parser_match(parser, TOKEN_LBRACKET)) {
        // Array literal [1, 2, 3]
        parser_advance(parser);
        node = AST_NODE(parser, AST_ARRAY_LITERAL);

        int capacity = 4;
        node->array_literal.elements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
        node->array_literal.count = 0;

        if (!parser_match(parser, TOKEN_RBRACKET)) {
            do {
                if (node->array_literal.count >= capacity) {
                    capacity *= 2;
                    node->array_literal.elements = (ASTNode**)realloc(
                        node->array_literal.elements, sizeof(ASTNode*) * capacity);
                }
                node->array_literal.elements[node->array_literal.count++] = parse_expression(parser);
            } while (parser_match(parser, TOKEN_COMMA) && (parser_advance(parser), true));
        }

        parser_expect(parser, TOKEN_RBRACKET);
    } else if (parser_match(parser, TOKEN_LBRACE)) {
        // Object literal { key: value, key2: value2 }
        parser_advance(parser);
        node = AST_NODE(parser, AST_OBJECT_LITERAL);

        int capacity = 4;
        node->object_literal.keys = (char**)malloc(sizeof(char*) * capacity);
        node->object_literal.values = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
        node->object_literal.count = 0;
        // Type info will be set during type inference in node->type_info

        if (!parser_match(parser, TOKEN_RBRACE)) {
            do {
                if (node->object_literal.count >= capacity) {
                    capacity *= 2;
                    node->object_literal.keys = (char**)realloc(
                        node->object_literal.keys, sizeof(char*) * capacity);
                    node->object_literal.values = (ASTNode**)realloc(
                        node->object_literal.values, sizeof(ASTNode*) * capacity);
                }

                // Parse property name (must be an identifier or string)
                if (!parser_match(parser, TOKEN_IDENTIFIER) && !parser_match(parser, TOKEN_STRING)) {
                    SourceLocation loc = {
                        .filename = parser->filename,
                        .line = parser->current_token->line,
                        .column = parser->current_token->column
                    };
                    log_error_at(&loc, "Expected property name in object literal");
                    return node;
                }

                node->object_literal.keys[node->object_literal.count] = strdup(parser->current_token->value);
                parser_advance(parser);

                // Expect colon
                parser_expect(parser, TOKEN_COLON);

                // Parse value
                node->object_literal.values[node->object_literal.count] = parse_expression(parser);
                node->object_literal.count++;

                // Check for comma (allows trailing comma)
                if (parser_match(parser, TOKEN_COMMA)) {
                    parser_advance(parser);
                    // If closing brace follows comma, break (trailing comma)
                    if (parser_match(parser, TOKEN_RBRACE)) {
                        break;
                    }
                } else {
                    // No comma, must be end of object
                    break;
                }

            } while (true);
        }

        parser_expect(parser, TOKEN_RBRACE);

        // NOTE: TypeInfo will be created during type inference with structural sharing
    } else {
        SourceLocation loc = {
            .filename = parser->filename,
            .line = parser->current_token->line,
            .column = parser->current_token->column
        };
        log_error_at(&loc, "Unexpected token in expression (type %d)", parser->current_token->type);
    }

    return node;
}

static ASTNode* parse_call(Parser* parser) {
    ASTNode* node = parse_primary(parser);

    if (!node) {
        return NULL; // parse_primary failed
    }

    while (parser_match(parser, TOKEN_LPAREN) || parser_match(parser, TOKEN_DOT) ||
           parser_match(parser, TOKEN_LBRACKET) || parser_match(parser, TOKEN_PLUSPLUS) ||
           parser_match(parser, TOKEN_MINUSMINUS)) {

        // Postfix ++ and --
        if (parser_match(parser, TOKEN_PLUSPLUS) || parser_match(parser, TOKEN_MINUSMINUS)) {
            // Allow postfix on identifiers, member access, and index access
            if (node->type == AST_IDENTIFIER) {
                ASTNode* postfix = AST_NODE(parser, AST_POSTFIX_OP);
                postfix->postfix_op.op = strdup(parser->current_token->value);
                postfix->postfix_op.name = strdup(node->identifier.name);
                postfix->postfix_op.target = NULL;
                parser_advance(parser);

                // Free the identifier node since we've copied its name
                ast_free(node);
                node = postfix;
                continue;
            } else if (node->type == AST_MEMBER_ACCESS || node->type == AST_INDEX_ACCESS) {
                ASTNode* postfix = AST_NODE(parser, AST_POSTFIX_OP);
                postfix->postfix_op.op = strdup(parser->current_token->value);
                postfix->postfix_op.name = NULL;
                postfix->postfix_op.target = node;  // Transfer ownership
                parser_advance(parser);

                node = postfix;
                continue;
            } else {
                SourceLocation loc = {
                    .filename = parser->filename,
                    .line = parser->current_token->line,
                    .column = parser->current_token->column
                };
                log_error_at(&loc, "Postfix operator can only be applied to identifiers or member access");
                return node;
            }
        }
        if (parser_match(parser, TOKEN_LBRACKET)) {
            // Index access: arr[0], str[2]
            parser_advance(parser);
            ASTNode* index_node = AST_NODE(parser, AST_INDEX_ACCESS);
            index_node->index_access.object = node;
            index_node->index_access.index = parse_expression(parser);
            parser_expect(parser, TOKEN_RBRACKET);
            node = index_node;
        } else if (parser_match(parser, TOKEN_DOT)) {
            parser_advance(parser);

            // Handle property access (e.g., console.log, obj.method)
            if (!parser_match(parser, TOKEN_IDENTIFIER)) {
                SourceLocation loc = {
                    .filename = parser->filename,
                    .line = parser->current_token->line,
                    .column = parser->current_token->column
                };
                log_error_at(&loc, "Expected identifier after '.'");
                return node;
            }

            // Create member access node
            ASTNode* member = AST_NODE(parser, AST_MEMBER_ACCESS);
            member->member_access.object = node;
            member->member_access.property = strdup(parser->current_token->value);
            parser_advance(parser);

            node = member;

        } else if (parser_match(parser, TOKEN_LPAREN)) {
            // Check if this is a method call (callee is member access)
            if (node->type == AST_MEMBER_ACCESS) {
                // Convert member access + call into method call
                ASTNode* method_call = AST_NODE(parser, AST_METHOD_CALL);
                method_call->method_call.object = node->member_access.object;
                method_call->method_call.method_name = node->member_access.property;
                method_call->method_call.args = NULL;
                method_call->method_call.arg_count = 0;
                method_call->method_call.is_static = false; // Will be determined in type inference
                
                // Free the member access node (but not its children, we reused them)
                free(node);
                
                parser_advance(parser); // consume '('

                if (!parser_match(parser, TOKEN_RPAREN)) {
                    int capacity = 4;
                    method_call->method_call.args = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);

                    do {
                        if (method_call->method_call.arg_count >= capacity) {
                            capacity *= 2;
                            method_call->method_call.args = (ASTNode**)realloc(method_call->method_call.args, sizeof(ASTNode*) * capacity);
                        }
                        method_call->method_call.args[method_call->method_call.arg_count++] = parse_expression(parser);
                    } while (parser_match(parser, TOKEN_COMMA) && (parser_advance(parser), true));
                }

                parser_expect(parser, TOKEN_RPAREN);
                node = method_call;
            } else {
                // Regular function call
                ASTNode* call = AST_NODE(parser, AST_CALL);
                call->call.callee = node;
                call->call.args = NULL;
                call->call.arg_count = 0;

                parser_advance(parser);

                if (!parser_match(parser, TOKEN_RPAREN)) {
                    int capacity = 4;
                    call->call.args = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);

                    do {
                        if (call->call.arg_count >= capacity) {
                            capacity *= 2;
                            call->call.args = (ASTNode**)realloc(call->call.args, sizeof(ASTNode*) * capacity);
                        }
                        call->call.args[call->call.arg_count++] = parse_expression(parser);
                    } while (parser_match(parser, TOKEN_COMMA) && (parser_advance(parser), true));
                }

                parser_expect(parser, TOKEN_RPAREN);
                node = call;
            }
        }
    }

    return node;
}

static ASTNode* parse_unary(Parser* parser) {
    // Prefix ++ and --
    if (parser_match(parser, TOKEN_PLUSPLUS) || parser_match(parser, TOKEN_MINUSMINUS)) {
        ASTNode* prefix = AST_NODE(parser, AST_PREFIX_OP);
        prefix->prefix_op.op = strdup(parser->current_token->value);
        parser_advance(parser);

        // Parse the target (identifier, member access, or index access)
        // We parse as primary and then check if we need member/index access
        ASTNode* target = parse_primary(parser);
        if (!target) {
            SourceLocation loc = {
                .filename = parser->filename,
                .line = parser->current_token->line,
                .column = parser->current_token->column
            };
            log_error_at(&loc, "Expected identifier or expression after %s", prefix->prefix_op.op);
            return NULL;
        }

        // Now check for member access or index access
        while (parser_match(parser, TOKEN_DOT) || parser_match(parser, TOKEN_LBRACKET)) {
            if (parser_match(parser, TOKEN_DOT)) {
                // Member access
                parser_advance(parser);
                if (!parser_match(parser, TOKEN_IDENTIFIER)) {
                    SourceLocation loc = {
                        .filename = parser->filename,
                        .line = parser->current_token->line,
                        .column = parser->current_token->column
                    };
                    log_error_at(&loc, "Expected property name after '.'");
                    return NULL;
                }

                ASTNode* member = AST_NODE(parser, AST_MEMBER_ACCESS);
                member->member_access.object = target;
                member->member_access.property = strdup(parser->current_token->value);
                parser_advance(parser);
                target = member;
            } else if (parser_match(parser, TOKEN_LBRACKET)) {
                // Index access
                parser_advance(parser);
                ASTNode* index = AST_NODE(parser, AST_INDEX_ACCESS);
                index->index_access.object = target;
                index->index_access.index = parse_expression(parser);

                if (!parser_match(parser, TOKEN_RBRACKET)) {
                    SourceLocation loc = {
                        .filename = parser->filename,
                        .line = parser->current_token->line,
                        .column = parser->current_token->column
                    };
                    log_error_at(&loc, "Expected ']' after index expression");
                    return NULL;
                }
                parser_advance(parser);
                target = index;
            }
        }

        // Now set the target appropriately
        if (target->type == AST_IDENTIFIER) {
            prefix->prefix_op.name = strdup(target->identifier.name);
            prefix->prefix_op.target = NULL;
            ast_free(target);
        } else {
            prefix->prefix_op.name = NULL;
            prefix->prefix_op.target = target;
        }

        return prefix;
    }

    // Other unary operators (including ref)
    if (parser_match(parser, TOKEN_MINUS) || parser_match(parser, TOKEN_NOT) || parser_match(parser, TOKEN_REF)) {
        ASTNode* node = AST_NODE(parser, AST_UNARY_OP);
        node->unary_op.op = strdup(parser->current_token->value);
        parser_advance(parser);
        node->unary_op.operand = parse_unary(parser);
        return node;
    }
    return parse_call(parser);
}

static ASTNode* parse_multiplicative(Parser* parser) {
    ASTNode* node = parse_unary(parser);

    while (parser_match(parser, TOKEN_STAR) || parser_match(parser, TOKEN_SLASH) || parser_match(parser, TOKEN_PERCENT)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_unary(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_additive(Parser* parser) {
    ASTNode* node = parse_multiplicative(parser);

    while (parser_match(parser, TOKEN_PLUS) || parser_match(parser, TOKEN_MINUS)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_multiplicative(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_bit_shift(Parser* parser) {
	ASTNode* node = parse_additive(parser);

	while (parser_match(parser, TOKEN_RIGHT_SHIFT) || parser_match(parser, TOKEN_LEFT_SHIFT)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_additive(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_comparison(Parser* parser) {
    ASTNode* node = parse_bit_shift(parser);

    while (parser_match(parser, TOKEN_LT) || parser_match(parser, TOKEN_GT) ||
           parser_match(parser, TOKEN_LE) || parser_match(parser, TOKEN_GE)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_additive(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_equality(Parser* parser) {
    ASTNode* node = parse_comparison(parser);

    while (parser_match(parser, TOKEN_EQ) || parser_match(parser, TOKEN_NE)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_comparison(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_bit_and(Parser* parser) {
	ASTNode* node = parse_equality(parser);

	while (parser_match(parser, TOKEN_BIT_AND)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_equality(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_bit_xor(Parser* parser) {
    ASTNode* node = parse_bit_and(parser);

    while (parser_match(parser, TOKEN_BIT_XOR)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_bit_and(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_bit_or(Parser* parser) {
    ASTNode* node = parse_bit_xor(parser);

    while (parser_match(parser, TOKEN_BIT_OR)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_bit_xor(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_logical_and(Parser* parser) {
    ASTNode* node = parse_bit_or(parser);

    while (parser_match(parser, TOKEN_AND)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_equality(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_logical_or(Parser* parser) {
    ASTNode* node = parse_logical_and(parser);

    while (parser_match(parser, TOKEN_OR)) {
        ASTNode* op = AST_NODE(parser, AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_logical_and(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_ternary(Parser* parser) {
    ASTNode* node = parse_logical_or(parser);

    if (parser_match(parser, TOKEN_QUESTION)) {
        parser_advance(parser);
        ASTNode* ternary = AST_NODE(parser, AST_TERNARY);
        ternary->ternary.condition = node;
        ternary->ternary.true_expr = parse_expression(parser);

        if (!parser_expect(parser, TOKEN_COLON)) {
            // parser_expect already logs the error
            ast_free(ternary);
            return node;
        }

        ternary->ternary.false_expr = parse_ternary(parser);
        return ternary;
    }

    return node;
}

static ASTNode* parse_assignment(Parser* parser) {
    ASTNode* node = parse_ternary(parser);

    // Check for compound assignment (+=, -=, *=, /=)
    if (parser_match(parser, TOKEN_PLUS_ASSIGN) || parser_match(parser, TOKEN_MINUS_ASSIGN) ||
        parser_match(parser, TOKEN_STAR_ASSIGN) || parser_match(parser, TOKEN_SLASH_ASSIGN)) {
        if (node->type == AST_IDENTIFIER) {
            ASTNode* compound = AST_NODE(parser, AST_COMPOUND_ASSIGNMENT);
            compound->compound_assignment.name = strdup(node->identifier.name);
            compound->compound_assignment.target = NULL;
            compound->compound_assignment.op = strdup(parser->current_token->value);
            ast_free(node);

            parser_advance(parser);
            compound->compound_assignment.value = parse_assignment(parser);
            return compound;
        } else if (node->type == AST_MEMBER_ACCESS || node->type == AST_INDEX_ACCESS) {
            ASTNode* compound = AST_NODE(parser, AST_COMPOUND_ASSIGNMENT);
            compound->compound_assignment.name = NULL;
            compound->compound_assignment.target = node;  // Transfer ownership
            compound->compound_assignment.op = strdup(parser->current_token->value);

            parser_advance(parser);
            compound->compound_assignment.value = parse_assignment(parser);
            return compound;
        } else {
            SourceLocation loc = {
                .filename = parser->filename,
                .line = parser->current_token->line,
                .column = parser->current_token->column
            };
            log_error_at(&loc, "Compound assignment requires identifier or member access on left side");
            return node;
        }
    }

    if (parser_match(parser, TOKEN_ASSIGN)) {
        // Handle regular variable assignment
        if (node->type == AST_IDENTIFIER) {
            ASTNode* assignment = AST_NODE(parser, AST_ASSIGNMENT);
            assignment->assignment.name = strdup(node->identifier.name);
            ast_free(node);

            parser_advance(parser);
            assignment->assignment.value = parse_assignment(parser);
            return assignment;
        }
        // Handle index assignment: arr[0] = value or str[2] = 'a'
        else if (node->type == AST_INDEX_ACCESS) {
            ASTNode* index_assignment = AST_NODE(parser, AST_INDEX_ASSIGNMENT);
            index_assignment->index_assignment.object = node->index_access.object;
            index_assignment->index_assignment.index = node->index_access.index;

            // Don't free the node entirely, just the wrapper
            free(node);

            parser_advance(parser);
            index_assignment->index_assignment.value = parse_assignment(parser);
            return index_assignment;
        }
        // Handle member assignment: obj.prop = value
        else if (node->type == AST_MEMBER_ACCESS) {
            ASTNode* member_assignment = AST_NODE(parser, AST_MEMBER_ASSIGNMENT);
            member_assignment->member_assignment.object = node->member_access.object;
            member_assignment->member_assignment.property = node->member_access.property;

            // Don't free property string, we're transferring ownership
            free(node);

            parser_advance(parser);
            member_assignment->member_assignment.value = parse_assignment(parser);
            return member_assignment;
        }
        else {
            SourceLocation loc = {
                .filename = parser->filename,
                .line = parser->current_token->line,
                .column = parser->current_token->column
            };
            log_error_at(&loc, "Invalid assignment target");
            return node;
        }
    }

    return node;
}

static ASTNode* parse_expression(Parser* parser) {
    return parse_assignment(parser);
}

static ASTNode* parse_block(Parser* parser) {
    ASTNode* node = AST_NODE(parser, AST_BLOCK);
    parser_expect(parser, TOKEN_LBRACE);

    int capacity = 8;
    node->block.statements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    node->block.count = 0;

    while (!parser_match(parser, TOKEN_RBRACE) && !parser_match(parser, TOKEN_EOF)) {
        if (node->block.count >= capacity) {
            capacity *= 2;
            node->block.statements = (ASTNode**)realloc(node->block.statements, sizeof(ASTNode*) * capacity);
        }
        ASTNode* stmt = parse_statement(parser);
        if (stmt) {
            node->block.statements[node->block.count++] = stmt;
        }
    }

    parser_expect(parser, TOKEN_RBRACE);
    return node;
}

static ASTNode* parse_var_declaration(Parser* parser) {
    bool is_const = parser_match(parser, TOKEN_CONST);
    parser_advance(parser); // skip 'var', 'let', or 'const'

    if (!parser_match(parser, TOKEN_IDENTIFIER)) {
        SourceLocation loc = {
            .filename = parser->filename,
            .line = parser->current_token->line,
            .column = parser->current_token->column
        };
        // Check if a type keyword was used as a variable name
        if (parser->current_token->type >= TOKEN_I8 && parser->current_token->type <= TOKEN_INT) {
            log_error_at(&loc, "Cannot use type keyword '%s' as variable name", 
                        parser->current_token->value);
        } else {
            log_error_at(&loc, "Expected identifier after var/let/const");
        }
        
        // Error recovery: skip to next semicolon or statement boundary
        while (!parser_match(parser, TOKEN_SEMICOLON) && 
               !parser_match(parser, TOKEN_EOF) &&
               !parser_match(parser, TOKEN_RBRACE)) {
            parser_advance(parser);
        }
        if (parser_match(parser, TOKEN_SEMICOLON)) {
            parser_advance(parser);
        }
        
        return NULL;
    }

    ASTNode* node = AST_NODE(parser, AST_VAR_DECL);
    node->var_decl.is_const = is_const;
    node->var_decl.name = strdup(parser->current_token->value);
    node->var_decl.array_size = 0;  // Default: not an array
    parser_advance(parser); // consume identifier

    // Parse optional type annotation (e.g., var x: int = 5)
    node->var_decl.type_hint = parse_type_annotation(parser);

    // Check for array size syntax: [size]
    if (parser_match(parser, TOKEN_LBRACKET)) {
        parser_advance(parser);  // consume '['
        
        // Parse array size expression
        // Use parse_ternary which handles arithmetic but stops at ] naturally
        ASTNode* size_expr = parse_ternary(parser);
        
        if (!size_expr) {
            log_error_at(SRC_LOC(parser->filename, parser->current_token->line, parser->current_token->column),
                        "Expected array size expression after '['");
            ast_free(node);
            return NULL;
        }
        
        // Store the expression
        node->var_decl.array_size = 0;  // Will be evaluated during type inference
        node->var_decl.array_size_expr = size_expr;
        
        if (!parser_match(parser, TOKEN_RBRACKET)) {
            log_error_at(SRC_LOC(parser->filename, parser->current_token->line, parser->current_token->column),
                        "Expected ']' after array size");
            ast_free(node);
            return NULL;
        }
        parser_advance(parser);  // consume ']'
        
        // Convert type_hint to array type
        if (node->var_decl.type_hint) {
            TypeInfo* array_type = type_info_create(TYPE_KIND_ARRAY, NULL);
            array_type->data.array.element_type = node->var_decl.type_hint;
            
            // Generate type name like "i32[]"
            char type_name[256];
            const char* elem_name = node->var_decl.type_hint->type_name ? node->var_decl.type_hint->type_name : "?";
            snprintf(type_name, sizeof(type_name), "%s[]", elem_name);
            array_type->type_name = strdup(type_name);
            
            node->var_decl.type_hint = array_type;
        }
    }

    if (parser_match(parser, TOKEN_ASSIGN)) {
        parser_advance(parser);
        node->var_decl.init = parse_expression(parser);
        if (!node->var_decl.init) {
            SourceLocation loc = {
                .filename = parser->filename,
                .line = parser->current_token->line,
                .column = parser->current_token->column
            };
            log_error_at(&loc, "Expected expression after =");
            ast_free(node);
            return NULL;
        }
    } else {
        node->var_decl.init = NULL;
    }

    if (parser_match(parser, TOKEN_SEMICOLON)) {
        parser_advance(parser);
    }

    return node;
}

static ASTNode* parse_function_declaration(Parser* parser) {
    parser_advance(parser); // skip 'function'

    ASTNode* node = AST_NODE(parser, AST_FUNCTION_DECL);
    node->func_decl.name = strdup(parser->current_token->value);
    parser_expect(parser, TOKEN_IDENTIFIER);

    parser_expect(parser, TOKEN_LPAREN);

    int capacity = 4;
    node->func_decl.params = (char**)malloc(sizeof(char*) * capacity);
    node->func_decl.param_type_hints = (TypeInfo**)malloc(sizeof(TypeInfo*) * capacity);
    node->func_decl.param_count = 0;
    node->func_decl.is_variadic = false;

    if (!parser_match(parser, TOKEN_RPAREN)) {
        do {
            if (node->func_decl.param_count >= capacity) {
                capacity *= 2;
                node->func_decl.params = (char**)realloc(node->func_decl.params, sizeof(char*) * capacity);
                node->func_decl.param_type_hints = (TypeInfo**)realloc(node->func_decl.param_type_hints, sizeof(TypeInfo*) * capacity);
            }
            node->func_decl.params[node->func_decl.param_count] = strdup(parser->current_token->value);
            parser_expect(parser, TOKEN_IDENTIFIER);

            // Parse optional type annotation for parameter (e.g., function add(a: int, b: int))
            node->func_decl.param_type_hints[node->func_decl.param_count] = parse_type_annotation(parser);
            node->func_decl.param_count++;
        } while (parser_match(parser, TOKEN_COMMA) && (parser_advance(parser), true));
    }

    parser_expect(parser, TOKEN_RPAREN);

    // Parse optional return type annotation (e.g., function add(...): int { })
    node->func_decl.return_type_hint = parse_type_annotation(parser);
    if (!node->func_decl.return_type_hint) {
        node->func_decl.return_type_hint = Type_Unknown;
    }

    node->func_decl.body = parse_block(parser);

    return node;
}

static ASTNode* parse_external_function_declaration(Parser* parser) {
    parser_advance(parser); // skip 'external'

    ASTNode* node = AST_NODE(parser, AST_FUNCTION_DECL);
    node->func_decl.name = strdup(parser->current_token->value);
    node->func_decl.body = NULL;  // External functions have no body
    parser_expect(parser, TOKEN_IDENTIFIER);

    parser_expect(parser, TOKEN_LPAREN);

    int capacity = 4;
    node->func_decl.params = (char**)malloc(sizeof(char*) * capacity);
    node->func_decl.param_type_hints = (TypeInfo**)malloc(sizeof(TypeInfo*) * capacity);
    node->func_decl.param_count = 0;
    node->func_decl.is_variadic = false;

    if (!parser_match(parser, TOKEN_RPAREN)) {
        do {
            // Check for variadic parameter (...)
            if (parser->current_token->type == TOKEN_ELLIPSIS) {
                node->func_decl.is_variadic = true;
                parser_advance(parser); // consume ...
                break; // ... must be the last parameter
            }
            
            if (node->func_decl.param_count >= capacity) {
                capacity *= 2;
                node->func_decl.params = (char**)realloc(node->func_decl.params, sizeof(char*) * capacity);
                node->func_decl.param_type_hints = (TypeInfo**)realloc(node->func_decl.param_type_hints, sizeof(TypeInfo*) * capacity);
            }
            
            // Check if parameter has a name or is just a type
            // Two cases:
            // 1. name:type - identifier followed by colon
            // 2. type - just the type name
            char* param_name = NULL;
            TypeInfo* param_type = NULL;
            
            if (parser->current_token->type == TOKEN_IDENTIFIER) {
                char* first_identifier = strdup(parser->current_token->value);
                parser_advance(parser);
                
                // Check if this is "name:type" or just "type"
                if (parser->current_token->type == TOKEN_COLON) {
                    // It's "name:type"
                    param_name = first_identifier;
                    param_type = parse_type_annotation(parser);
                    if (!param_type) {
                        SourceLocation loc = {
                            .filename = parser->filename,
                            .line = parser->current_token->line,
                            .column = parser->current_token->column
                        };
                        log_error_at(&loc, "External function parameters must have type annotations");
                        free(first_identifier);
                        return node;
                    }
                } else {
                    // It's just "type" - the identifier is the type name
                    // Look up the type by name
                    param_type = type_context_find_type(parser->type_ctx, first_identifier);
                    if (!param_type) {
                        SourceLocation loc = {
                            .filename = parser->filename,
                            .line = parser->current_token->line,
                            .column = parser->current_token->column
                        };
                        log_error_at(&loc, "Unknown type '%s' in external function parameter", first_identifier);
                        free(first_identifier);
                        return node;
                    }
                    // Generate a placeholder name for the parameter
                    char buffer[32];
                    snprintf(buffer, sizeof(buffer), "param%d", node->func_decl.param_count);
                    param_name = strdup(buffer);
                    free(first_identifier);
                }
            } else {
                SourceLocation loc = {
                    .filename = parser->filename,
                    .line = parser->current_token->line,
                    .column = parser->current_token->column
                };
                log_error_at(&loc, "Expected parameter name or type in external function declaration");
                return node;
            }
            
            node->func_decl.params[node->func_decl.param_count] = param_name;
            node->func_decl.param_type_hints[node->func_decl.param_count] = param_type;
            node->func_decl.param_count++;
        } while (parser_match(parser, TOKEN_COMMA) && (parser_advance(parser), true));
    }

    parser_expect(parser, TOKEN_RPAREN);

    // Parse REQUIRED return type annotation
    node->func_decl.return_type_hint = parse_type_annotation(parser);
    if (!node->func_decl.return_type_hint) {
        SourceLocation loc = {
            .filename = parser->filename,
            .line = parser->current_token->line,
            .column = parser->current_token->column
        };
        log_error_at(&loc, "External function must have return type annotation");
        return node;
    }

    parser_expect(parser, TOKEN_SEMICOLON);

    return node;
}

static ASTNode* parse_struct_declaration(Parser* parser) {
    parser_advance(parser); // skip 'struct'

    ASTNode* node = AST_NODE(parser, AST_STRUCT_DECL);
    
    // Parse struct name
    if (parser->current_token->type != TOKEN_IDENTIFIER) {
        SourceLocation loc = {
            .filename = parser->filename,
            .line = parser->current_token->line,
            .column = parser->current_token->column
        };
        log_error_at(&loc, "Expected struct name after 'struct' keyword");
        return node;
    }
    
    node->struct_decl.name = strdup(parser->current_token->value);
    parser_advance(parser);
    
    parser_expect(parser, TOKEN_LBRACE);
    
    // Register struct type EARLY so methods can reference it
    // We'll register with empty properties first, then update after parsing
    if (parser->type_ctx) {
        TypeInfo* struct_type = type_context_create_struct_type(
            parser->type_ctx,
            node->struct_decl.name,
            NULL,  // No properties yet
            NULL,
            0,
            NULL
        );
        
        if (struct_type) {
            log_verbose("Pre-registered struct type during parsing: %s", node->struct_decl.name);
        }
    }
    
    // Parse properties and methods
    int capacity = 4;
    node->struct_decl.property_names = (char**)malloc(sizeof(char*) * capacity);
    node->struct_decl.property_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * capacity);
    node->struct_decl.default_values = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    node->struct_decl.property_array_sizes = (int*)malloc(sizeof(int) * capacity);
    node->struct_decl.property_array_size_exprs = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    node->struct_decl.property_count = 0;
    
    int method_capacity = 4;
    node->struct_decl.methods = (ASTNode**)malloc(sizeof(ASTNode*) * method_capacity);
    node->struct_decl.method_count = 0;
    
    while (!parser_match(parser, TOKEN_RBRACE) && !parser_match(parser, TOKEN_EOF)) {
        if (node->struct_decl.property_count >= capacity) {
            capacity *= 2;
            node->struct_decl.property_names = (char**)realloc(node->struct_decl.property_names, sizeof(char*) * capacity);
            node->struct_decl.property_types = (TypeInfo**)realloc(node->struct_decl.property_types, sizeof(TypeInfo*) * capacity);
            node->struct_decl.default_values = (ASTNode**)realloc(node->struct_decl.default_values, sizeof(ASTNode*) * capacity);
            node->struct_decl.property_array_sizes = (int*)realloc(node->struct_decl.property_array_sizes, sizeof(int) * capacity);
            node->struct_decl.property_array_size_exprs = (ASTNode**)realloc(node->struct_decl.property_array_size_exprs, sizeof(ASTNode*) * capacity);
        }
        
        // Parse property name or method name
        if (parser->current_token->type != TOKEN_IDENTIFIER) {
            SourceLocation loc = {
                .filename = parser->filename,
                .line = parser->current_token->line,
                .column = parser->current_token->column
            };
            log_error_at(&loc, "Expected property or method name in struct declaration");
            return node;
        }
        
        char* member_name = strdup(parser->current_token->value);
        parser_advance(parser);
        
        // Check if this is a method (next token is '(') or a property (next token is ':')
        if (parser_match(parser, TOKEN_LPAREN)) {
            // This is a method declaration
            if (node->struct_decl.method_count >= method_capacity) {
                method_capacity *= 2;
                node->struct_decl.methods = (ASTNode**)realloc(node->struct_decl.methods, sizeof(ASTNode*) * method_capacity);
            }
            
            // Parse the function declaration (params, return type, body)
            // No need to consume 'function' keyword anymore - directly at '('
            
            ASTNode* method = AST_NODE(parser, AST_FUNCTION_DECL);
            method->func_decl.name = strdup(member_name);
            method->func_decl.is_variadic = false;
            
            // Parse parameters
            parser_expect(parser, TOKEN_LPAREN);
            
            int param_capacity = 4;
            method->func_decl.params = (char**)malloc(sizeof(char*) * param_capacity);
            method->func_decl.param_type_hints = (TypeInfo**)malloc(sizeof(TypeInfo*) * param_capacity);
            method->func_decl.param_count = 0;
            
            while (!parser_match(parser, TOKEN_RPAREN) && !parser_match(parser, TOKEN_EOF)) {
                if (method->func_decl.param_count >= param_capacity) {
                    param_capacity *= 2;
                    method->func_decl.params = (char**)realloc(method->func_decl.params, sizeof(char*) * param_capacity);
                    method->func_decl.param_type_hints = (TypeInfo**)realloc(method->func_decl.param_type_hints, sizeof(TypeInfo*) * param_capacity);
                }
                
                // Check for variadic (...)
                if (parser_match(parser, TOKEN_ELLIPSIS)) {
                    method->func_decl.is_variadic = true;
                    parser_advance(parser);
                    break;
                }
                
                // Parse parameter name
                if (parser->current_token->type != TOKEN_IDENTIFIER) {
                    SourceLocation loc = {
                        .filename = parser->filename,
                        .line = parser->current_token->line,
                        .column = parser->current_token->column
                    };
                    log_error_at(&loc, "Expected parameter name");
                    free(member_name);
                    return node;
                }
                
                char* param_name = strdup(parser->current_token->value);
                parser_advance(parser);
                
                // Parse type annotation (required for methods)
                TypeInfo* param_type = parse_type_annotation(parser);
                if (!param_type) {
                    SourceLocation loc = {
                        .filename = parser->filename,
                        .line = parser->current_token->line,
                        .column = parser->current_token->column
                    };
                    log_error_at(&loc, "Method parameter '%s' must have a type annotation", param_name);
                    free(param_name);
                    free(member_name);
                    return node;
                }
                
                method->func_decl.params[method->func_decl.param_count] = param_name;
                method->func_decl.param_type_hints[method->func_decl.param_count] = param_type;
                method->func_decl.param_count++;
                
                if (parser_match(parser, TOKEN_COMMA)) {
                    parser_advance(parser);
                }
            }
            
            parser_expect(parser, TOKEN_RPAREN);
            
            // Parse return type annotation (required for methods)
            method->func_decl.return_type_hint = parse_type_annotation(parser);
            if (!method->func_decl.return_type_hint) {
                SourceLocation loc = {
                    .filename = parser->filename,
                    .line = parser->current_token->line,
                    .column = parser->current_token->column
                };
                log_error_at(&loc, "Method '%s' must have a return type annotation", member_name);
                free(member_name);
                return node;
            }
            
            // Parse method body
            method->func_decl.body = parse_block(parser);
            
            node->struct_decl.methods[node->struct_decl.method_count] = method;
            node->struct_decl.method_count++;
            
            free(member_name);
            continue; // Continue to next member
        }
        
        // This is a property - parse type annotation (required)
        TypeInfo* prop_type = parse_type_annotation(parser);
        if (!prop_type) {
            SourceLocation loc = {
                .filename = parser->filename,
                .line = parser->current_token->line,
                .column = parser->current_token->column
            };
            log_error_at(&loc, "Struct property '%s' must have a type annotation", member_name);
            free(member_name);
            return node;
        }
        
        // Check for array syntax [size]
        int array_size = 0;
        ASTNode* array_size_expr = NULL;
        if (parser_match(parser, TOKEN_LBRACKET)) {
            parser_advance(parser); // consume '['
            
            // Parse array size expression (number, identifier, or const expression)
            array_size_expr = parse_ternary(parser);
            
            if (!array_size_expr) {
                log_error_at(SRC_LOC(parser->filename, parser->current_token->line, parser->current_token->column),
                            "Expected array size expression after '['");
                free(member_name);
                return node;
            }
            
            // Store expression for evaluation during type inference
            // array_size will be set to 0 and evaluated later
            array_size = 0;
            
            if (!parser_match(parser, TOKEN_RBRACKET)) {
                SourceLocation loc = {
                    .filename = parser->filename,
                    .line = parser->current_token->line,
                    .column = parser->current_token->column
                };
                log_error_at(&loc, "Expected ']' after array size");
                free(member_name);
                return node;
            }
            parser_advance(parser); // consume ']'
            
            // If array size is specified, wrap the type in an array type
            TypeInfo* array_type = type_info_create(TYPE_KIND_ARRAY, NULL);
            array_type->data.array.element_type = prop_type;
            
            // Generate type name like "i32[]" (generic - size is in property_array_sizes)
            char type_name[256];
            const char* elem_name = prop_type->type_name ? prop_type->type_name : "?";
            snprintf(type_name, sizeof(type_name), "%s[]", elem_name);
            array_type->type_name = strdup(type_name);
            
            prop_type = array_type;
        }
        
        // If prop_type is already an array type (from parse_type_annotation returning array)
        // but we don't have array_size or array_size_expr, that means it's like "arr: i32[]" which we don't support
        if (array_size == 0 && !array_size_expr && type_info_is_array(prop_type)) {
            SourceLocation loc = {
                .filename = parser->filename,
                .line = parser->current_token->line,
                .column = parser->current_token->column
            };
            log_error_at(&loc, "Array fields in structs must have explicit size (e.g., arr: i32[12]). Generic array types i32[] are not supported yet.");
            free(member_name);
            return node;
        }
        
        // Check for default value (optional)
        ASTNode* default_value = NULL;
        if (parser_match(parser, TOKEN_ASSIGN)) {
            parser_advance(parser); // consume '='
            
            // Parse literal value only
            if (parser->current_token->type == TOKEN_NUMBER) {
                default_value = parse_primary(parser);
            } else if (parser->current_token->type == TOKEN_STRING) {
                default_value = parse_primary(parser);
            } else if (parser->current_token->type == TOKEN_TRUE || parser->current_token->type == TOKEN_FALSE) {
                default_value = parse_primary(parser);
            } else {
                SourceLocation loc = {
                    .filename = parser->filename,
                    .line = parser->current_token->line,
                    .column = parser->current_token->column
                };
                log_error_at(&loc, "Default values must be literals (number, string, true, or false)");
                free(member_name);
                return node;
            }
            
            // Validate default value type matches property type
            // Type inference will handle the actual validation
        }
        
        node->struct_decl.property_names[node->struct_decl.property_count] = member_name;
        node->struct_decl.property_types[node->struct_decl.property_count] = prop_type;
        node->struct_decl.default_values[node->struct_decl.property_count] = default_value;
        node->struct_decl.property_array_sizes[node->struct_decl.property_count] = array_size;
        node->struct_decl.property_array_size_exprs[node->struct_decl.property_count] = array_size_expr;
        node->struct_decl.property_count++;
        
        // Expect comma or semicolon after property
        if (parser_match(parser, TOKEN_COMMA)) {
            parser_advance(parser);
        } else {
            parser_expect(parser, TOKEN_SEMICOLON);
        }
    }
    
    parser_expect(parser, TOKEN_RBRACE);
    
    // Update struct type with properties (it was pre-registered before parsing members)
    if (parser->type_ctx) {
        TypeInfo* struct_type = type_context_find_struct_type(parser->type_ctx, node->struct_decl.name);
        if (struct_type && struct_type->data.object.property_count == 0) {
            // Update the properties
            struct_type->data.object.property_names = node->struct_decl.property_names;
            struct_type->data.object.property_types = node->struct_decl.property_types;
            struct_type->data.object.property_count = node->struct_decl.property_count;
            struct_type->data.object.struct_decl_node = node;
            
            log_verbose("Updated struct type with properties: %s (%d properties)", 
                       node->struct_decl.name, node->struct_decl.property_count);
        }
    }
    
    return node;
}

static ASTNode* parse_return_statement(Parser* parser) {
    parser_advance(parser); // skip 'return'

    ASTNode* node = AST_NODE(parser, AST_RETURN);

    if (!parser_match(parser, TOKEN_SEMICOLON) && !parser_match(parser, TOKEN_RBRACE)) {
        node->return_stmt.value = parse_expression(parser);
    } else {
        node->return_stmt.value = NULL;
    }

    if (parser_match(parser, TOKEN_SEMICOLON)) {
        parser_advance(parser);
    }

    return node;
}

static ASTNode* parse_break_statement(Parser* parser) {
    parser_advance(parser); // skip 'break'

    ASTNode* node = AST_NODE(parser, AST_BREAK);

    if (parser_match(parser, TOKEN_SEMICOLON)) {
        parser_advance(parser);
    }

    return node;
}

static ASTNode* parse_continue_statement(Parser* parser) {
    parser_advance(parser); // skip 'continue'

    ASTNode* node = AST_NODE(parser, AST_CONTINUE);

    if (parser_match(parser, TOKEN_SEMICOLON)) {
        parser_advance(parser);
    }

    return node;
}

static ASTNode* parse_if_statement(Parser* parser) {
    parser_advance(parser); // skip 'if'

    ASTNode* node = AST_NODE(parser, AST_IF);
    parser_expect(parser, TOKEN_LPAREN);
    node->if_stmt.condition = parse_expression(parser);
    parser_expect(parser, TOKEN_RPAREN);

    node->if_stmt.then_branch = parse_statement(parser);

    if (parser_match(parser, TOKEN_ELSE)) {
        parser_advance(parser);
        node->if_stmt.else_branch = parse_statement(parser);
    } else {
        node->if_stmt.else_branch = NULL;
    }

    return node;
}

static ASTNode* parse_for_statement(Parser* parser) {
    parser_advance(parser); // skip 'for'

    ASTNode* node = AST_NODE(parser, AST_FOR);
    parser_expect(parser, TOKEN_LPAREN);

    if (parser_match(parser, TOKEN_VAR) || parser_match(parser, TOKEN_LET) || parser_match(parser, TOKEN_CONST)) {
        node->for_stmt.init = parse_var_declaration(parser);
    } else if (!parser_match(parser, TOKEN_SEMICOLON)) {
        ASTNode* expr = AST_NODE(parser, AST_EXPR_STMT);
        expr->expr_stmt.expression = parse_expression(parser);
        node->for_stmt.init = expr;
        parser_expect(parser, TOKEN_SEMICOLON);
    } else {
        node->for_stmt.init = NULL;
        parser_advance(parser);
    }

    if (!parser_match(parser, TOKEN_SEMICOLON)) {
        node->for_stmt.condition = parse_expression(parser);
    } else {
        node->for_stmt.condition = NULL;
    }
    parser_expect(parser, TOKEN_SEMICOLON);

    if (!parser_match(parser, TOKEN_RPAREN)) {
        node->for_stmt.update = parse_expression(parser);
    } else {
        node->for_stmt.update = NULL;
    }
    parser_expect(parser, TOKEN_RPAREN);

    node->for_stmt.body = parse_statement(parser);

    return node;
}

static ASTNode* parse_while_statement(Parser* parser) {
    parser_advance(parser); // skip 'while'

    ASTNode* node = AST_NODE(parser, AST_WHILE);
    parser_expect(parser, TOKEN_LPAREN);
    node->while_stmt.condition = parse_expression(parser);
    parser_expect(parser, TOKEN_RPAREN);

    node->while_stmt.body = parse_statement(parser);

    return node;
}

static ASTNode* parse_statement(Parser* parser) {
    if (parser_match(parser, TOKEN_VAR) || parser_match(parser, TOKEN_LET) || parser_match(parser, TOKEN_CONST)) {
        return parse_var_declaration(parser);
    } else if (parser_match(parser, TOKEN_FUNCTION)) {
        return parse_function_declaration(parser);
    } else if (parser_match(parser, TOKEN_EXTERNAL)) {
        return parse_external_function_declaration(parser);
    } else if (parser_match(parser, TOKEN_STRUCT)) {
        return parse_struct_declaration(parser);
    } else if (parser_match(parser, TOKEN_RETURN)) {
        return parse_return_statement(parser);
    } else if (parser_match(parser, TOKEN_BREAK)) {
        return parse_break_statement(parser);
    } else if (parser_match(parser, TOKEN_CONTINUE)) {
        return parse_continue_statement(parser);
    } else if (parser_match(parser, TOKEN_IF)) {
        return parse_if_statement(parser);
    } else if (parser_match(parser, TOKEN_FOR)) {
        return parse_for_statement(parser);
    } else if (parser_match(parser, TOKEN_WHILE)) {
        return parse_while_statement(parser);
    } else if (parser_match(parser, TOKEN_LBRACE)) {
        return parse_block(parser);
    } else if (parser_match(parser, TOKEN_SEMICOLON)) {
        // Empty statement - just consume the semicolon
        parser_advance(parser);
        return NULL;
    } else {
        // Expression statement (including function calls like console.log())
        ASTNode* expr = parse_expression(parser);
        if (!expr) {
            // If expression parsing failed, skip to next semicolon or newline to recover
            while (!parser_match(parser, TOKEN_SEMICOLON) &&
                   !parser_match(parser, TOKEN_EOF) &&
                   !parser_match(parser, TOKEN_RBRACE)) {
                parser_advance(parser);
            }
            if (parser_match(parser, TOKEN_SEMICOLON)) {
                parser_advance(parser);
            }
            return NULL;
        }

        ASTNode* node = AST_NODE(parser, AST_EXPR_STMT);
        node->expr_stmt.expression = expr;

        if (parser_match(parser, TOKEN_SEMICOLON)) {
            parser_advance(parser);
        }
        return node;
    }
}

ASTNode* parser_parse(Parser* parser) {
    ASTNode* program = AST_NODE(parser, AST_PROGRAM);

    int capacity = 16;
    program->program.statements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    program->program.count = 0;

    while (!parser_match(parser, TOKEN_EOF)) {
        // Check if we have any errors - abort parsing to prevent segfaults
        if (logger_has_errors()) {
            // Don't bother freeing - we're about to exit anyway
            // and partial AST cleanup can cause double-free errors
            return NULL;
        }
        
        // Save current position to detect if we're stuck
        size_t prev_line = parser->current_token->line;
        size_t prev_col = parser->current_token->column;
        TokenType prev_type = parser->current_token->type;

        if (program->program.count >= capacity) {
            capacity *= 2;
            program->program.statements = (ASTNode**)realloc(program->program.statements, sizeof(ASTNode*) * capacity);
        }

        ASTNode* stmt = parse_statement(parser);

        // Check if parser advanced - if not, we're stuck
        if (parser->current_token->line == prev_line &&
            parser->current_token->column == prev_col &&
            parser->current_token->type == prev_type &&
            !parser_match(parser, TOKEN_EOF)) {
            SourceLocation loc = {
                .filename = parser->filename,
                .line = parser->current_token->line,
                .column = parser->current_token->column
            };
            log_error_at(&loc, "Stuck on token type %d, value '%s'",
                    parser->current_token->type,
                    parser->current_token->value ? parser->current_token->value : "(null)");
            // Skip the problematic token to avoid infinite loop
            parser_advance(parser);
            if (stmt) ast_free(stmt);
            continue;
        }

        if (stmt) {
            program->program.statements[program->program.count++] = stmt;
        }
    }

    return program;
}
