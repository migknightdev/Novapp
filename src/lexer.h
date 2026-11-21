// lexer.h — Part 1 of the novac project: tokenizer for Nova++.
//
// Turns raw .nvpp source text into a flat stream of typed Tokens. This is the
// first stage of a real compiler pipeline (lexer -> parser/AST -> semantic
// analysis -> codegen), replacing vcomp.cpp's line-by-line text substitution
// approach. Nothing here produces C++ or touches g++ — this stage only knows
// about characters and tokens.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace novac {

enum class TokenType {
    // Literals & identifiers
    IDENTIFIER,
    NUMBER,
    STRING,
    TRUE_LIT,
    FALSE_LIT,

    // Keywords
    KW_FUNC,
    KW_IF,
    KW_ELIF,
    KW_ELSE,
    KW_WHILE,
    KW_FOR,
    KW_IN,
    KW_USING,
    KW_NOMAIN,
    KW_IMPORT,
    KW_FROM,
    KW_LET,
    KW_CONST,
    KW_RETURN,
    KW_PF,
    KW_INPUT,
    KW_SLEEP,
    KW_EXIT,
    KW_BREAK,
    KW_CONTINUE,
    KW_SWITCH,
    KW_CASE,
    KW_DEFAULT,

    // Punctuation
    LPAREN,     // (
    RPAREN,     // )
    LBRACE,     // {
    RBRACE,     // }
    LBRACKET,   // [
    RBRACKET,   // ]
    DOT,        // .
    COMMA,      // ,
    SEMICOLON,  // ;
    COLON,      // :
    HASH,       // #

    // Operators
    ASSIGN,         // =
    EQ,             // ==
    NEQ,            // !=
    LT,             // <
    GT,             // >
    LE,             // <=
    GE,             // >=
    AND,            // &&
    OR,             // ||
    NOT,            // !
    PLUS,           // +
    MINUS,          // -
    STAR,           // *
    SLASH,          // /
    PERCENT,        // %
    PLUS_ASSIGN,    // +=
    MINUS_ASSIGN,   // -=
    STAR_ASSIGN,    // *=
    SLASH_ASSIGN,   // /=
    PLUS_PLUS,      // ++
    MINUS_MINUS,    // --

    // Structural
    NEWLINE,
    END_OF_FILE,
    UNKNOWN
};

// Human-readable name for a TokenType (used in error messages and tests).
const char* token_type_name(TokenType type);

struct Token {
    TokenType type = TokenType::UNKNOWN;
    std::string lexeme;      // raw source text for this token
    std::string str_value;   // decoded value for STRING tokens (escapes resolved)
    double num_value = 0.0;  // decoded value for NUMBER tokens
    bool is_float = false;   // whether NUMBER contained a '.'
    int line = 1;
    int col = 1;
};

struct LexError {
    std::string message;
    int line;
    int col;
};

class Lexer {
public:
    explicit Lexer(std::string source, std::string filename = "<input>");

    // Tokenizes the entire source. Always returns a token stream (ending in
    // END_OF_FILE) even if errors were encountered — bad tokens are skipped
    // so the caller can still see downstream structure. Check errors() to
    // know whether tokenization was clean.
    std::vector<Token> tokenize();

    const std::vector<LexError>& errors() const { return errors_; }
    bool ok() const { return errors_.empty(); }

private:
    std::string source_;
    std::string filename_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    std::vector<LexError> errors_;

    bool at_end() const;
    char peek(int offset = 0) const;
    char advance();
    bool match(char expected);

    void add_error(const std::string& message, int line, int col);

    Token make_token(TokenType type, const std::string& lexeme, int start_line, int start_col);

    void skip_whitespace_and_comments(std::vector<Token>& out);
    Token read_string(int start_line, int start_col);
    Token read_number(int start_line, int start_col);
    Token read_identifier_or_keyword(int start_line, int start_col);
};

} // namespace novac
