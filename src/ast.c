#include <stdlib.h>
#include <string.h>

#include "js_compiler.h"

ASTNode* ast_create(ASTNodeType type) {
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
