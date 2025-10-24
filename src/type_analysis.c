#include "js_compiler.h"
#include <string.h>

static void type_analyze_node(ASTNode* node, SymbolTable* symbols);

// Helper to infer function return type from return statements
static ValueType infer_function_return_type(ASTNode* node) {
    if (!node) return TYPE_VOID;

    switch (node->type) {
        case AST_RETURN:
            if (node->return_stmt.value) {
                return node->return_stmt.value->value_type;
            }
            return TYPE_VOID;

        case AST_BLOCK:
        case AST_PROGRAM:
            // Check all statements for return
            for (int i = 0; i < node->program.count; i++) {
                ValueType ret_type = infer_function_return_type(node->program.statements[i]);
                if (ret_type != TYPE_VOID && ret_type != TYPE_UNKNOWN) {
                    return ret_type;
                }
            }
            return TYPE_VOID;

        case AST_IF:
            // Check both branches
            {
                ValueType then_type = infer_function_return_type(node->if_stmt.then_branch);
                if (then_type != TYPE_VOID && then_type != TYPE_UNKNOWN) {
                    return then_type;
                }
                if (node->if_stmt.else_branch) {
                    ValueType else_type = infer_function_return_type(node->if_stmt.else_branch);
                    if (else_type != TYPE_VOID && else_type != TYPE_UNKNOWN) {
                        return else_type;
                    }
                }
            }
            return TYPE_VOID;

        case AST_FOR:
        case AST_WHILE:
            return infer_function_return_type(node->for_stmt.body);

        default:
            return TYPE_VOID;
    }
}

static ValueType infer_binary_type(const char* op, ValueType left, ValueType right) {
    // String concatenation with +
    if (strcmp(op, "+") == 0) {
        if (left == TYPE_STRING || right == TYPE_STRING) {
            return TYPE_STRING;
        }
        if (left == TYPE_DOUBLE || right == TYPE_DOUBLE) {
            return TYPE_DOUBLE;
        }
        return TYPE_INT;
    }

    // Arithmetic operations
    if (strcmp(op, "-") == 0 || strcmp(op, "*") == 0 || strcmp(op, "/") == 0) {
        if (left == TYPE_DOUBLE || right == TYPE_DOUBLE) {
            return TYPE_DOUBLE;
        }
        return TYPE_INT;
    }

    // Comparison operations
    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
        strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        return TYPE_BOOL;
    }

    return TYPE_UNKNOWN;
}

static void type_analyze_node(ASTNode* node, SymbolTable* symbols) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                type_analyze_node(node->program.statements[i], symbols);
            }
            break;

        case AST_VAR_DECL:
            if (node->var_decl.init) {
                type_analyze_node(node->var_decl.init, symbols);
                node->value_type = node->var_decl.init->value_type;
            } else {
                node->value_type = TYPE_INT; // Default to int
            }
            symbol_table_insert(symbols, node->var_decl.name, node->value_type, NULL);
            break;

        case AST_FUNCTION_DECL: {
            // Create new scope for function
            SymbolTable* func_scope = symbol_table_create(symbols);

            // Add parameters to function scope
            for (int i = 0; i < node->func_decl.param_count; i++) {
                symbol_table_insert(func_scope, node->func_decl.params[i], TYPE_UNKNOWN, NULL);
            }

            // Analyze function body
            type_analyze_node(node->func_decl.body, func_scope);

            // Infer return type from return statements in body
            node->func_decl.return_type = infer_function_return_type(node->func_decl.body);

            symbol_table_free(func_scope);

            // Register function in current scope
            symbol_table_insert(symbols, node->func_decl.name, node->func_decl.return_type, NULL);
            break;
        }

        case AST_RETURN:
            if (node->return_stmt.value) {
                type_analyze_node(node->return_stmt.value, symbols);
                node->value_type = node->return_stmt.value->value_type;
            } else {
                node->value_type = TYPE_VOID;
            }
            break;

        case AST_IF:
            type_analyze_node(node->if_stmt.condition, symbols);
            type_analyze_node(node->if_stmt.then_branch, symbols);
            type_analyze_node(node->if_stmt.else_branch, symbols);
            break;

        case AST_FOR:
            type_analyze_node(node->for_stmt.init, symbols);
            type_analyze_node(node->for_stmt.condition, symbols);
            type_analyze_node(node->for_stmt.update, symbols);
            type_analyze_node(node->for_stmt.body, symbols);
            break;

        case AST_WHILE:
            type_analyze_node(node->while_stmt.condition, symbols);
            type_analyze_node(node->while_stmt.body, symbols);
            break;

        case AST_EXPR_STMT:
            type_analyze_node(node->expr_stmt.expression, symbols);
            break;

        case AST_BINARY_OP:
            type_analyze_node(node->binary_op.left, symbols);
            type_analyze_node(node->binary_op.right, symbols);
            node->value_type = infer_binary_type(
                node->binary_op.op,
                node->binary_op.left->value_type,
                node->binary_op.right->value_type
            );
            break;

        case AST_UNARY_OP:
            type_analyze_node(node->unary_op.operand, symbols);
            if (strcmp(node->unary_op.op, "!") == 0) {
                node->value_type = TYPE_BOOL;
            } else {
                node->value_type = node->unary_op.operand->value_type;
            }
            break;

        case AST_CALL:
            type_analyze_node(node->call.callee, symbols);
            for (int i = 0; i < node->call.arg_count; i++) {
                type_analyze_node(node->call.args[i], symbols);
            }

            // Special case for console.log
            if (node->call.callee->type == AST_IDENTIFIER &&
                strcmp(node->call.callee->identifier.name, "console.log") == 0) {
                node->value_type = TYPE_VOID;
            } else {
                // For now, assume unknown return type
                node->value_type = TYPE_UNKNOWN;
            }
            break;

        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(symbols, node->identifier.name);
            if (entry) {
                node->value_type = entry->type;
            } else {
                node->value_type = TYPE_UNKNOWN;
            }
            break;
        }

        case AST_ASSIGNMENT:
            type_analyze_node(node->assignment.value, symbols);
            node->value_type = node->assignment.value->value_type;

            // Update symbol table
            SymbolEntry* entry = symbol_table_lookup(symbols, node->assignment.name);
            if (entry) {
                entry->type = node->value_type;
            }
            break;

        case AST_MEMBER_ACCESS:
            type_analyze_node(node->member_access.object, symbols);
            // For now, member access returns unknown type
            // This will be refined when we have proper object support
            node->value_type = TYPE_UNKNOWN;
            break;

        case AST_NUMBER:
        case AST_STRING:
        case AST_BOOLEAN:
            // Type already set during parsing
            break;

        default:
            break;
    }
}

void type_analyze(ASTNode* node, SymbolTable* symbols) {
    type_analyze_node(node, symbols);
}
