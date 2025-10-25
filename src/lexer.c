#include "jsasta_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Lexer* lexer_create(const char* source) {
    Lexer* lexer = (Lexer*)malloc(sizeof(Lexer));
    lexer->source = source;
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->current = source[0];
    return lexer;
}

void lexer_free(Lexer* lexer) {
    free(lexer);
}

static void lexer_advance(Lexer* lexer) {
    if (lexer->current == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    lexer->position++;
    lexer->current = lexer->source[lexer->position];
}

static void lexer_skip_whitespace(Lexer* lexer) {
    while (lexer->current == ' ' || lexer->current == '\t' ||
           lexer->current == '\n' || lexer->current == '\r') {
        lexer_advance(lexer);
    }
}

static void lexer_skip_comment(Lexer* lexer) {
    if (lexer->current == '/' && lexer->source[lexer->position + 1] == '/') {
        while (lexer->current != '\n' && lexer->current != '\0') {
            lexer_advance(lexer);
        }
    } else if (lexer->current == '/' && lexer->source[lexer->position + 1] == '*') {
        lexer_advance(lexer);
        lexer_advance(lexer);
        while (!(lexer->current == '*' && lexer->source[lexer->position + 1] == '/')) {
            if (lexer->current == '\0') break;
            lexer_advance(lexer);
        }
        if (lexer->current == '*') {
            lexer_advance(lexer);
            lexer_advance(lexer);
        }
    }
}

static Token* token_create(TokenType type, const char* value, size_t line, size_t column) {
    Token* token = (Token*)malloc(sizeof(Token));
    token->type = type;
    token->value = value ? strdup(value) : NULL;
    token->line = line;
    token->column = column;
    return token;
}

void token_free(Token* token) {
    if (token->value) free(token->value);
    free(token);
}

static Token* lexer_read_number(Lexer* lexer) {
    size_t start_line = lexer->line;
    size_t start_col = lexer->column;
    char buffer[256];
    int i = 0;

    while (isdigit(lexer->current) || lexer->current == '.') {
        buffer[i++] = lexer->current;
        lexer_advance(lexer);
    }
    buffer[i] = '\0';

    return token_create(TOKEN_NUMBER, buffer, start_line, start_col);
}

static Token* lexer_read_string(Lexer* lexer) {
    size_t start_line = lexer->line;
    size_t start_col = lexer->column;
    char quote = lexer->current;
    char buffer[1024];
    int i = 0;

    lexer_advance(lexer); // skip opening quote

    while (lexer->current != quote && lexer->current != '\0') {
        if (lexer->current == '\\') {
            lexer_advance(lexer);
            switch (lexer->current) {
                case 'n': buffer[i++] = '\n'; break;
                case 't': buffer[i++] = '\t'; break;
                case 'r': buffer[i++] = '\r'; break;
                case 'e': buffer[i++] = '\033'; break;  // ESC character for ANSI codes
                case '\\': buffer[i++] = '\\'; break;
                case '"': buffer[i++] = '"'; break;
                case '\'': buffer[i++] = '\''; break;
                default: buffer[i++] = lexer->current;
            }
        } else {
            buffer[i++] = lexer->current;
        }
        lexer_advance(lexer);
    }
    buffer[i] = '\0';

    if (lexer->current == quote) {
        lexer_advance(lexer); // skip closing quote
    }

    return token_create(TOKEN_STRING, buffer, start_line, start_col);
}

static Token* lexer_read_identifier(Lexer* lexer) {
    size_t start_line = lexer->line;
    size_t start_col = lexer->column;
    char buffer[256];
    int i = 0;

    while (isalnum(lexer->current) || lexer->current == '_' || lexer->current == '$') {
        buffer[i++] = lexer->current;
        lexer_advance(lexer);
    }
    buffer[i] = '\0';

    // Check for keywords
    TokenType type = TOKEN_IDENTIFIER;
    if (strcmp(buffer, "var") == 0) type = TOKEN_VAR;
    else if (strcmp(buffer, "let") == 0) type = TOKEN_LET;
    else if (strcmp(buffer, "const") == 0) type = TOKEN_CONST;
    else if (strcmp(buffer, "function") == 0) type = TOKEN_FUNCTION;
    else if (strcmp(buffer, "return") == 0) type = TOKEN_RETURN;
    else if (strcmp(buffer, "if") == 0) type = TOKEN_IF;
    else if (strcmp(buffer, "else") == 0) type = TOKEN_ELSE;
    else if (strcmp(buffer, "for") == 0) type = TOKEN_FOR;
    else if (strcmp(buffer, "while") == 0) type = TOKEN_WHILE;
    else if (strcmp(buffer, "true") == 0) type = TOKEN_TRUE;
    else if (strcmp(buffer, "false") == 0) type = TOKEN_FALSE;

    return token_create(type, buffer, start_line, start_col);
}

Token* lexer_next_token(Lexer* lexer) {
    while (lexer->current != '\0') {
        if (isspace(lexer->current)) {
            lexer_skip_whitespace(lexer);
            continue;
        }

        if (lexer->current == '/' && (lexer->source[lexer->position + 1] == '/' ||
                                      lexer->source[lexer->position + 1] == '*')) {
            lexer_skip_comment(lexer);
            continue;
        }

        size_t line = lexer->line;
        size_t col = lexer->column;

        if (isdigit(lexer->current)) {
            return lexer_read_number(lexer);
        }

        if (lexer->current == '"' || lexer->current == '\'') {
            return lexer_read_string(lexer);
        }

        if (isalpha(lexer->current) || lexer->current == '_' || lexer->current == '$') {
            return lexer_read_identifier(lexer);
        }

        char ch = lexer->current;
        lexer_advance(lexer);

        switch (ch) {
            case '+':
                if (lexer->current == '+') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_PLUSPLUS, "++", line, col);
                } else if (lexer->current == '=') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_PLUS_ASSIGN, "+=", line, col);
                }
                return token_create(TOKEN_PLUS, "+", line, col);
            case '-':
                if (lexer->current == '-') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_MINUSMINUS, "--", line, col);
                } else if (lexer->current == '=') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_MINUS_ASSIGN, "-=", line, col);
                }
                return token_create(TOKEN_MINUS, "-", line, col);
            case '*':
                if (lexer->current == '=') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_STAR_ASSIGN, "*=", line, col);
                }
                return token_create(TOKEN_STAR, "*", line, col);
            case '%': return token_create(TOKEN_PERCENT, "%", line, col);
            case '/':
                if (lexer->current == '=') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_SLASH_ASSIGN, "/=", line, col);
                }
                return token_create(TOKEN_SLASH, "/", line, col);
            case '(': return token_create(TOKEN_LPAREN, "(", line, col);
            case ')': return token_create(TOKEN_RPAREN, ")", line, col);
            case '{': return token_create(TOKEN_LBRACE, "{", line, col);
            case '}': return token_create(TOKEN_RBRACE, "}", line, col);
            case '[': return token_create(TOKEN_LBRACKET, "[", line, col);
            case ']': return token_create(TOKEN_RBRACKET, "]", line, col);
            case ';': return token_create(TOKEN_SEMICOLON, ";", line, col);
            case ',': return token_create(TOKEN_COMMA, ",", line, col);
            case '.': return token_create(TOKEN_DOT, ".", line, col);
            case '?': return token_create(TOKEN_QUESTION, "?", line, col);
            case ':': return token_create(TOKEN_COLON, ":", line, col);
            case '=':
                if (lexer->current == '=') {
                    lexer_advance(lexer);
                    if (lexer->current == '=') {
                        lexer_advance(lexer);
                    }
                    return token_create(TOKEN_EQ, "==", line, col);
                }
                return token_create(TOKEN_ASSIGN, "=", line, col);
            case '!':
                if (lexer->current == '=') {
                    lexer_advance(lexer);
                    if (lexer->current == '=') {
                        lexer_advance(lexer);
                    }
                    return token_create(TOKEN_NE, "!=", line, col);
                }
                return token_create(TOKEN_NOT, "!", line, col);
            case '<':
		            if (lexer->current == '>') {
									lexer_advance(lexer);
                  return token_create(TOKEN_LEFT_SHIFT, "<<", line, col);
								} else if (lexer->current == '=') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_LE, "<=", line, col);
                }
                return token_create(TOKEN_LT, "<", line, col);
            case '>':
		            if (lexer->current == '>') {
										lexer_advance(lexer);
                    return token_create(TOKEN_RIGHT_SHIFT, ">>", line, col);
								} else if (lexer->current == '=') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_GE, ">=", line, col);
                }
                return token_create(TOKEN_GT, ">", line, col);
            case '&':
                if (lexer->current == '&') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_AND, "&&", line, col);
                }
                return token_create(TOKEN_BIT_AND, "&", line, col);
            case '|':
                if (lexer->current == '|') {
                    lexer_advance(lexer);
                    return token_create(TOKEN_OR, "||", line, col);
                }
                break;
        }
    }

    return token_create(TOKEN_EOF, NULL, lexer->line, lexer->column);
}
