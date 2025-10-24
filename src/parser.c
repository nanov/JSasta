#include "js_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        fprintf(stderr, "Parse error at line %d: expected token type %d, got %d\n",
                parser->current_token->line, type, parser->current_token->type);
        return false;
    }
    parser_advance(parser);
    return true;
}

Parser* parser_create(const char* source) {
    Parser* parser = (Parser*)malloc(sizeof(Parser));
    parser->lexer = lexer_create(source);
    parser->current_token = NULL;
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

static ASTNode* ast_create(ASTNodeType type) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = type;
    node->value_type = TYPE_UNKNOWN;
    return node;
}

void ast_free(ASTNode* node) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                ast_free(node->program.statements[i]);
            }
            free(node->program.statements);
            break;

        case AST_VAR_DECL:
            free(node->var_decl.name);
            ast_free(node->var_decl.init);
            break;

        case AST_FUNCTION_DECL:
            free(node->func_decl.name);
            for (int i = 0; i < node->func_decl.param_count; i++) {
                free(node->func_decl.params[i]);
            }
            free(node->func_decl.params);
            free(node->func_decl.param_types);
            ast_free(node->func_decl.body);
            break;

        case AST_RETURN:
            ast_free(node->return_stmt.value);
            break;

        case AST_IF:
            ast_free(node->if_stmt.condition);
            ast_free(node->if_stmt.then_branch);
            ast_free(node->if_stmt.else_branch);
            break;

        case AST_FOR:
            ast_free(node->for_stmt.init);
            ast_free(node->for_stmt.condition);
            ast_free(node->for_stmt.update);
            ast_free(node->for_stmt.body);
            break;

        case AST_WHILE:
            ast_free(node->while_stmt.condition);
            ast_free(node->while_stmt.body);
            break;

        case AST_EXPR_STMT:
            ast_free(node->expr_stmt.expression);
            break;

        case AST_BINARY_OP:
            free(node->binary_op.op);
            ast_free(node->binary_op.left);
            ast_free(node->binary_op.right);
            break;

        case AST_UNARY_OP:
            free(node->unary_op.op);
            ast_free(node->unary_op.operand);
            break;

        case AST_CALL:
            ast_free(node->call.callee);
            for (int i = 0; i < node->call.arg_count; i++) {
                ast_free(node->call.args[i]);
            }
            free(node->call.args);
            break;

        case AST_IDENTIFIER:
            free(node->identifier.name);
            break;

        case AST_STRING:
            free(node->string.value);
            break;

        case AST_ASSIGNMENT:
            free(node->assignment.name);
            ast_free(node->assignment.value);
            break;

        case AST_MEMBER_ACCESS:
            ast_free(node->member_access.object);
            free(node->member_access.property);
            break;

        default:
            break;
    }

    free(node);
}

ASTNode* ast_clone(ASTNode* node) {
    if (!node) return NULL;

    ASTNode* clone = (ASTNode*)calloc(1, sizeof(ASTNode));
    clone->type = node->type;
    clone->value_type = node->value_type;
    clone->specialization_ctx = NULL; // Don't clone specialization context

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            clone->program.count = node->program.count;
            clone->program.statements = (ASTNode**)malloc(sizeof(ASTNode*) * node->program.count);
            for (int i = 0; i < node->program.count; i++) {
                clone->program.statements[i] = ast_clone(node->program.statements[i]);
            }
            break;

        case AST_VAR_DECL:
            clone->var_decl.name = strdup(node->var_decl.name);
            clone->var_decl.init = ast_clone(node->var_decl.init);
            clone->var_decl.is_const = node->var_decl.is_const;
            break;

        case AST_FUNCTION_DECL:
            clone->func_decl.name = strdup(node->func_decl.name);
            clone->func_decl.param_count = node->func_decl.param_count;

            clone->func_decl.params = (char**)malloc(sizeof(char*) * node->func_decl.param_count);
            for (int i = 0; i < node->func_decl.param_count; i++) {
                clone->func_decl.params[i] = strdup(node->func_decl.params[i]);
            }

            if (node->func_decl.param_types) {
                clone->func_decl.param_types = (ValueType*)malloc(sizeof(ValueType) * node->func_decl.param_count);
                memcpy(clone->func_decl.param_types, node->func_decl.param_types,
                       sizeof(ValueType) * node->func_decl.param_count);
            }

            clone->func_decl.body = ast_clone(node->func_decl.body);
            clone->func_decl.return_type = node->func_decl.return_type;
            break;

        case AST_RETURN:
            clone->return_stmt.value = ast_clone(node->return_stmt.value);
            break;

        case AST_IF:
            clone->if_stmt.condition = ast_clone(node->if_stmt.condition);
            clone->if_stmt.then_branch = ast_clone(node->if_stmt.then_branch);
            clone->if_stmt.else_branch = ast_clone(node->if_stmt.else_branch);
            break;

        case AST_FOR:
            clone->for_stmt.init = ast_clone(node->for_stmt.init);
            clone->for_stmt.condition = ast_clone(node->for_stmt.condition);
            clone->for_stmt.update = ast_clone(node->for_stmt.update);
            clone->for_stmt.body = ast_clone(node->for_stmt.body);
            break;

        case AST_WHILE:
            clone->while_stmt.condition = ast_clone(node->while_stmt.condition);
            clone->while_stmt.body = ast_clone(node->while_stmt.body);
            break;

        case AST_EXPR_STMT:
            clone->expr_stmt.expression = ast_clone(node->expr_stmt.expression);
            break;

        case AST_BINARY_OP:
            clone->binary_op.op = strdup(node->binary_op.op);
            clone->binary_op.left = ast_clone(node->binary_op.left);
            clone->binary_op.right = ast_clone(node->binary_op.right);
            break;

        case AST_UNARY_OP:
            clone->unary_op.op = strdup(node->unary_op.op);
            clone->unary_op.operand = ast_clone(node->unary_op.operand);
            break;

        case AST_CALL:
            clone->call.callee = ast_clone(node->call.callee);
            clone->call.arg_count = node->call.arg_count;
            clone->call.args = (ASTNode**)malloc(sizeof(ASTNode*) * node->call.arg_count);
            for (int i = 0; i < node->call.arg_count; i++) {
                clone->call.args[i] = ast_clone(node->call.args[i]);
            }
            break;

        case AST_IDENTIFIER:
            clone->identifier.name = strdup(node->identifier.name);
            break;

        case AST_NUMBER:
            clone->number.value = node->number.value;
            break;

        case AST_STRING:
            clone->string.value = strdup(node->string.value);
            break;

        case AST_BOOLEAN:
            clone->boolean.value = node->boolean.value;
            break;

        case AST_ASSIGNMENT:
            clone->assignment.name = strdup(node->assignment.name);
            clone->assignment.value = ast_clone(node->assignment.value);
            break;

        case AST_MEMBER_ACCESS:
            clone->member_access.object = ast_clone(node->member_access.object);
            clone->member_access.property = strdup(node->member_access.property);
            break;
    }

    return clone;
}

// Forward declarations
static ASTNode* parse_statement(Parser* parser);
static ASTNode* parse_expression(Parser* parser);

static ASTNode* parse_primary(Parser* parser) {
    ASTNode* node = NULL;

    if (parser_match(parser, TOKEN_NUMBER)) {
        node = ast_create(AST_NUMBER);
        node->number.value = atof(parser->current_token->value);
        node->value_type = strchr(parser->current_token->value, '.') ? TYPE_DOUBLE : TYPE_INT;
        parser_advance(parser);
    } else if (parser_match(parser, TOKEN_STRING)) {
        node = ast_create(AST_STRING);
        node->string.value = strdup(parser->current_token->value);
        node->value_type = TYPE_STRING;
        parser_advance(parser);
    } else if (parser_match(parser, TOKEN_TRUE) || parser_match(parser, TOKEN_FALSE)) {
        node = ast_create(AST_BOOLEAN);
        node->boolean.value = parser_match(parser, TOKEN_TRUE);
        node->value_type = TYPE_BOOL;
        parser_advance(parser);
    } else if (parser_match(parser, TOKEN_IDENTIFIER)) {
        node = ast_create(AST_IDENTIFIER);
        node->identifier.name = strdup(parser->current_token->value);
        parser_advance(parser);
    } else if (parser_match(parser, TOKEN_LPAREN)) {
        parser_advance(parser);
        node = parse_expression(parser);
        parser_expect(parser, TOKEN_RPAREN);
    } else {
        fprintf(stderr, "Parse error at line %d, column %d: unexpected token in expression (type %d)\n",
                parser->current_token->line, parser->current_token->column,
                parser->current_token->type);
    }

    return node;
}

static ASTNode* parse_call(Parser* parser) {
    ASTNode* node = parse_primary(parser);

    if (!node) {
        return NULL; // parse_primary failed
    }

    while (parser_match(parser, TOKEN_LPAREN) || parser_match(parser, TOKEN_DOT)) {
        if (parser_match(parser, TOKEN_DOT)) {
            parser_advance(parser);

            // Handle property access (e.g., console.log, obj.method)
            if (!parser_match(parser, TOKEN_IDENTIFIER)) {
                fprintf(stderr, "Parse error at line %d: expected identifier after '.'\n",
                        parser->current_token->line);
                return node;
            }

            // Create member access node
            ASTNode* member = ast_create(AST_MEMBER_ACCESS);
            member->member_access.object = node;
            member->member_access.property = strdup(parser->current_token->value);
            parser_advance(parser);

            node = member;

        } else if (parser_match(parser, TOKEN_LPAREN)) {
            ASTNode* call = ast_create(AST_CALL);
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

    return node;
}

static ASTNode* parse_unary(Parser* parser) {
    if (parser_match(parser, TOKEN_MINUS) || parser_match(parser, TOKEN_NOT)) {
        ASTNode* node = ast_create(AST_UNARY_OP);
        node->unary_op.op = strdup(parser->current_token->value);
        parser_advance(parser);
        node->unary_op.operand = parse_unary(parser);
        return node;
    }
    return parse_call(parser);
}

static ASTNode* parse_multiplicative(Parser* parser) {
    ASTNode* node = parse_unary(parser);

    while (parser_match(parser, TOKEN_STAR) || parser_match(parser, TOKEN_SLASH)) {
        ASTNode* op = ast_create(AST_BINARY_OP);
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
        ASTNode* op = ast_create(AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_multiplicative(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_comparison(Parser* parser) {
    ASTNode* node = parse_additive(parser);

    while (parser_match(parser, TOKEN_LT) || parser_match(parser, TOKEN_GT) ||
           parser_match(parser, TOKEN_LE) || parser_match(parser, TOKEN_GE)) {
        ASTNode* op = ast_create(AST_BINARY_OP);
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
        ASTNode* op = ast_create(AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_comparison(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_logical_and(Parser* parser) {
    ASTNode* node = parse_equality(parser);

    while (parser_match(parser, TOKEN_AND)) {
        ASTNode* op = ast_create(AST_BINARY_OP);
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
        ASTNode* op = ast_create(AST_BINARY_OP);
        op->binary_op.op = strdup(parser->current_token->value);
        op->binary_op.left = node;
        parser_advance(parser);
        op->binary_op.right = parse_logical_and(parser);
        node = op;
    }

    return node;
}

static ASTNode* parse_assignment(Parser* parser) {
    ASTNode* node = parse_logical_or(parser);

    if (parser_match(parser, TOKEN_ASSIGN)) {
        if (node->type != AST_IDENTIFIER) {
            fprintf(stderr, "Parse error: invalid assignment target\n");
            return node;
        }

        ASTNode* assignment = ast_create(AST_ASSIGNMENT);
        assignment->assignment.name = strdup(node->identifier.name);
        ast_free(node);

        parser_advance(parser);
        assignment->assignment.value = parse_assignment(parser);
        return assignment;
    }

    return node;
}

static ASTNode* parse_expression(Parser* parser) {
    return parse_assignment(parser);
}

static ASTNode* parse_block(Parser* parser) {
    ASTNode* node = ast_create(AST_BLOCK);
    parser_expect(parser, TOKEN_LBRACE);

    int capacity = 8;
    node->block.statements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    node->block.count = 0;

    while (!parser_match(parser, TOKEN_RBRACE) && !parser_match(parser, TOKEN_EOF)) {
        if (node->block.count >= capacity) {
            capacity *= 2;
            node->block.statements = (ASTNode**)realloc(node->block.statements, sizeof(ASTNode*) * capacity);
        }
        node->block.statements[node->block.count++] = parse_statement(parser);
    }

    parser_expect(parser, TOKEN_RBRACE);
    return node;
}

static ASTNode* parse_var_declaration(Parser* parser) {
    bool is_const = false;
    parser_advance(parser); // skip 'var' or 'let'

    if (!parser_match(parser, TOKEN_IDENTIFIER)) {
        fprintf(stderr, "Parse error at line %d: expected identifier after var/let\n",
                parser->current_token->line);
        return NULL;
    }

    ASTNode* node = ast_create(AST_VAR_DECL);
    node->var_decl.is_const = is_const;
    node->var_decl.name = strdup(parser->current_token->value);
    parser_advance(parser); // consume identifier

    if (parser_match(parser, TOKEN_ASSIGN)) {
        parser_advance(parser);
        node->var_decl.init = parse_expression(parser);
        if (!node->var_decl.init) {
            fprintf(stderr, "Parse error at line %d: expected expression after =\n",
                    parser->current_token->line);
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

    ASTNode* node = ast_create(AST_FUNCTION_DECL);
    node->func_decl.name = strdup(parser->current_token->value);
    parser_expect(parser, TOKEN_IDENTIFIER);

    parser_expect(parser, TOKEN_LPAREN);

    int capacity = 4;
    node->func_decl.params = (char**)malloc(sizeof(char*) * capacity);
    node->func_decl.param_types = (ValueType*)malloc(sizeof(ValueType) * capacity);
    node->func_decl.param_count = 0;

    if (!parser_match(parser, TOKEN_RPAREN)) {
        do {
            if (node->func_decl.param_count >= capacity) {
                capacity *= 2;
                node->func_decl.params = (char**)realloc(node->func_decl.params, sizeof(char*) * capacity);
                node->func_decl.param_types = (ValueType*)realloc(node->func_decl.param_types, sizeof(ValueType) * capacity);
            }
            node->func_decl.params[node->func_decl.param_count] = strdup(parser->current_token->value);
            node->func_decl.param_types[node->func_decl.param_count] = TYPE_UNKNOWN;
            node->func_decl.param_count++;
            parser_expect(parser, TOKEN_IDENTIFIER);
        } while (parser_match(parser, TOKEN_COMMA) && (parser_advance(parser), true));
    }

    parser_expect(parser, TOKEN_RPAREN);
    node->func_decl.body = parse_block(parser);
    node->func_decl.return_type = TYPE_UNKNOWN;

    return node;
}

static ASTNode* parse_return_statement(Parser* parser) {
    parser_advance(parser); // skip 'return'

    ASTNode* node = ast_create(AST_RETURN);

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

static ASTNode* parse_if_statement(Parser* parser) {
    parser_advance(parser); // skip 'if'

    ASTNode* node = ast_create(AST_IF);
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

    ASTNode* node = ast_create(AST_FOR);
    parser_expect(parser, TOKEN_LPAREN);

    if (parser_match(parser, TOKEN_VAR) || parser_match(parser, TOKEN_LET)) {
        node->for_stmt.init = parse_var_declaration(parser);
    } else if (!parser_match(parser, TOKEN_SEMICOLON)) {
        ASTNode* expr = ast_create(AST_EXPR_STMT);
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

    ASTNode* node = ast_create(AST_WHILE);
    parser_expect(parser, TOKEN_LPAREN);
    node->while_stmt.condition = parse_expression(parser);
    parser_expect(parser, TOKEN_RPAREN);

    node->while_stmt.body = parse_statement(parser);

    return node;
}

static ASTNode* parse_statement(Parser* parser) {
    if (parser_match(parser, TOKEN_VAR) || parser_match(parser, TOKEN_LET)) {
        return parse_var_declaration(parser);
    } else if (parser_match(parser, TOKEN_FUNCTION)) {
        return parse_function_declaration(parser);
    } else if (parser_match(parser, TOKEN_RETURN)) {
        return parse_return_statement(parser);
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

        ASTNode* node = ast_create(AST_EXPR_STMT);
        node->expr_stmt.expression = expr;

        if (parser_match(parser, TOKEN_SEMICOLON)) {
            parser_advance(parser);
        }
        return node;
    }
}

ASTNode* parser_parse(Parser* parser) {
    ASTNode* program = ast_create(AST_PROGRAM);

    int capacity = 16;
    program->program.statements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    program->program.count = 0;

    while (!parser_match(parser, TOKEN_EOF)) {
        // Save current position to detect if we're stuck
        int prev_line = parser->current_token->line;
        int prev_col = parser->current_token->column;
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
            fprintf(stderr, "Parse error at line %d, column %d: stuck on token type %d, value '%s'\n",
                    parser->current_token->line, parser->current_token->column,
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
