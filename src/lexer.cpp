// lexer.cpp — implementation of the novac tokenizer.
#include "lexer.h"

#include <unordered_map>
#include <cctype>
#include <cstdlib>

namespace novac {

const char* token_type_name(TokenType type) {
    switch (type) {
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::STRING: return "STRING";
        case TokenType::TRUE_LIT: return "TRUE";
        case TokenType::FALSE_LIT: return "FALSE";
        case TokenType::KW_FUNC: return "func";
        case TokenType::KW_IF: return "if";
        case TokenType::KW_ELIF: return "elif";
        case TokenType::KW_ELSE: return "else";
        case TokenType::KW_WHILE: return "while";
        case TokenType::KW_FOR: return "for";
        case TokenType::KW_IN: return "in";
        case TokenType::KW_USING: return "using";
        case TokenType::KW_NOMAIN: return "noMain";
        case TokenType::KW_IMPORT: return "import";
        case TokenType::KW_FROM: return "From";
        case TokenType::KW_LET: return "let";
        case TokenType::KW_CONST: return "const";
        case TokenType::KW_RETURN: return "return";
        case TokenType::KW_PF: return "pf";
        case TokenType::KW_INPUT: return "input";
        case TokenType::KW_SLEEP: return "sleep";
        case TokenType::KW_EXIT: return "exit";
        case TokenType::KW_BREAK: return "break";
        case TokenType::KW_CONTINUE: return "continue";
        case TokenType::KW_SWITCH: return "switch";
        case TokenType::KW_CASE: return "case";
        case TokenType::KW_DEFAULT: return "default";
        case TokenType::LPAREN: return "(";
        case TokenType::RPAREN: return ")";
        case TokenType::LBRACE: return "{";
        case TokenType::RBRACE: return "}";
        case TokenType::LBRACKET: return "[";
        case TokenType::RBRACKET: return "]";
        case TokenType::DOT: return ".";
        case TokenType::COMMA: return ",";
        case TokenType::SEMICOLON: return ";";
        case TokenType::COLON: return ":";
        case TokenType::HASH: return "#";
        case TokenType::ASSIGN: return "=";
        case TokenType::EQ: return "==";
        case TokenType::NEQ: return "!=";
        case TokenType::LT: return "<";
        case TokenType::GT: return ">";
        case TokenType::LE: return "<=";
        case TokenType::GE: return ">=";
        case TokenType::AND: return "&&";
        case TokenType::OR: return "||";
        case TokenType::NOT: return "!";
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::STAR: return "*";
        case TokenType::SLASH: return "/";
        case TokenType::PERCENT: return "%";
        case TokenType::PLUS_ASSIGN: return "+=";
        case TokenType::MINUS_ASSIGN: return "-=";
        case TokenType::STAR_ASSIGN: return "*=";
        case TokenType::SLASH_ASSIGN: return "/=";
        case TokenType::PLUS_PLUS: return "++";
        case TokenType::MINUS_MINUS: return "--";
        case TokenType::NEWLINE: return "NEWLINE";
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::UNKNOWN: return "UNKNOWN";
    }
    return "?";
}

namespace {
const std::unordered_map<std::string, TokenType>& keyword_table() {
    static const std::unordered_map<std::string, TokenType> table = {
        {"func", TokenType::KW_FUNC},
        {"if", TokenType::KW_IF},
        {"elif", TokenType::KW_ELIF},
        {"else", TokenType::KW_ELSE},
        {"while", TokenType::KW_WHILE},
        {"for", TokenType::KW_FOR},
        {"in", TokenType::KW_IN},
        {"using", TokenType::KW_USING},
        {"noMain", TokenType::KW_NOMAIN},
        {"import", TokenType::KW_IMPORT},
        {"From", TokenType::KW_FROM},
        {"from", TokenType::KW_FROM},
        {"let", TokenType::KW_LET},
        {"const", TokenType::KW_CONST},
        {"return", TokenType::KW_RETURN},
        {"pf", TokenType::KW_PF},
        {"input", TokenType::KW_INPUT},
        {"sleep", TokenType::KW_SLEEP},
        {"exit", TokenType::KW_EXIT},
        {"break", TokenType::KW_BREAK},
        {"continue", TokenType::KW_CONTINUE},
        {"switch", TokenType::KW_SWITCH},
        {"case", TokenType::KW_CASE},
        {"default", TokenType::KW_DEFAULT},
        {"true", TokenType::TRUE_LIT},
        {"false", TokenType::FALSE_LIT},
    };
    return table;
}
} // namespace

Lexer::Lexer(std::string source, std::string filename)
    : source_(std::move(source)), filename_(std::move(filename)) {}

bool Lexer::at_end() const { return pos_ >= source_.size(); }

char Lexer::peek(int offset) const {
    size_t idx = pos_ + static_cast<size_t>(offset);
    if (idx >= source_.size()) return '\0';
    return source_[idx];
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (at_end() || source_[pos_] != expected) return false;
    advance();
    return true;
}

void Lexer::add_error(const std::string& message, int line, int col) {
    errors_.push_back(LexError{message, line, col});
}

Token Lexer::make_token(TokenType type, const std::string& lexeme, int start_line, int start_col) {
    Token t;
    t.type = type;
    t.lexeme = lexeme;
    t.line = start_line;
    t.col = start_col;
    return t;
}

Token Lexer::read_string(int start_line, int start_col) {
    std::string value;
    bool terminated = false;
    while (!at_end()) {
        char c = peek();
        if (c == '"') { advance(); terminated = true; break; }
        if (c == '\n') break; // strings don't span lines in Nova++
        if (c == '\\') {
            advance();
            char esc = at_end() ? '\0' : advance();
            switch (esc) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case '\0':
                    add_error("Unterminated escape sequence at end of file.", line_, col_);
                    break;
                default:
                    // Unknown escape: keep both characters literally, matching
                    // a forgiving "best effort" policy rather than hard-failing.
                    value += '\\';
                    value += esc;
                    break;
            }
            continue;
        }
        value += advance();
    }
    if (!terminated) {
        add_error("Unterminated string literal.", start_line, start_col);
    }
    Token t = make_token(TokenType::STRING, value, start_line, start_col);
    t.str_value = value;
    return t;
}

Token Lexer::read_number(int start_line, int start_col) {
    std::string lexeme;
    bool is_float = false;
    while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
        lexeme += advance();
    }
    if (!at_end() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        is_float = true;
        lexeme += advance(); // consume '.'
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            lexeme += advance();
        }
    }
    Token t = make_token(TokenType::NUMBER, lexeme, start_line, start_col);
    t.is_float = is_float;
    t.num_value = std::strtod(lexeme.c_str(), nullptr);
    return t;
}

Token Lexer::read_identifier_or_keyword(int start_line, int start_col) {
    std::string lexeme;
    while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        lexeme += advance();
    }
    const auto& kws = keyword_table();
    auto it = kws.find(lexeme);
    TokenType type = (it != kws.end()) ? it->second : TokenType::IDENTIFIER;
    return make_token(type, lexeme, start_line, start_col);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!at_end()) {
        char c = peek();

        // --- whitespace (not newlines) ---
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
            continue;
        }

        // --- newline: significant token (statement separator) ---
        if (c == '\n') {
            int sl = line_, sc = col_;
            advance();
            tokens.push_back(make_token(TokenType::NEWLINE, "\\n", sl, sc));
            continue;
        }

        // --- comments: // to end of line ---
        if (c == '/' && peek(1) == '/') {
            while (!at_end() && peek() != '\n') advance();
            continue;
        }

        int start_line = line_, start_col = col_;

        // --- string literal ---
        if (c == '"') {
            advance(); // consume opening quote
            tokens.push_back(read_string(start_line, start_col));
            continue;
        }

        // --- number literal ---
        if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(read_number(start_line, start_col));
            continue;
        }

        // --- identifier / keyword ---
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(read_identifier_or_keyword(start_line, start_col));
            continue;
        }

        // --- operators & punctuation ---
        advance(); // consume c
        switch (c) {
            case '(': tokens.push_back(make_token(TokenType::LPAREN, "(", start_line, start_col)); break;
            case ')': tokens.push_back(make_token(TokenType::RPAREN, ")", start_line, start_col)); break;
            case '{': tokens.push_back(make_token(TokenType::LBRACE, "{", start_line, start_col)); break;
            case '}': tokens.push_back(make_token(TokenType::RBRACE, "}", start_line, start_col)); break;
            case '[': tokens.push_back(make_token(TokenType::LBRACKET, "[", start_line, start_col)); break;
            case ']': tokens.push_back(make_token(TokenType::RBRACKET, "]", start_line, start_col)); break;
            case '.': tokens.push_back(make_token(TokenType::DOT, ".", start_line, start_col)); break;
            case ',': tokens.push_back(make_token(TokenType::COMMA, ",", start_line, start_col)); break;
            case ';': tokens.push_back(make_token(TokenType::SEMICOLON, ";", start_line, start_col)); break;
            case ':': tokens.push_back(make_token(TokenType::COLON, ":", start_line, start_col)); break;
            case '#': tokens.push_back(make_token(TokenType::HASH, "#", start_line, start_col)); break;

            case '=':
                if (match('=')) tokens.push_back(make_token(TokenType::EQ, "==", start_line, start_col));
                else tokens.push_back(make_token(TokenType::ASSIGN, "=", start_line, start_col));
                break;
            case '!':
                if (match('=')) tokens.push_back(make_token(TokenType::NEQ, "!=", start_line, start_col));
                else tokens.push_back(make_token(TokenType::NOT, "!", start_line, start_col));
                break;
            case '<':
                if (match('=')) tokens.push_back(make_token(TokenType::LE, "<=", start_line, start_col));
                else tokens.push_back(make_token(TokenType::LT, "<", start_line, start_col));
                break;
            case '>':
                if (match('=')) tokens.push_back(make_token(TokenType::GE, ">=", start_line, start_col));
                else tokens.push_back(make_token(TokenType::GT, ">", start_line, start_col));
                break;
            case '&':
                if (match('&')) tokens.push_back(make_token(TokenType::AND, "&&", start_line, start_col));
                else { add_error("Unexpected character '&' (did you mean '&&'?).", start_line, start_col); }
                break;
            case '|':
                if (match('|')) tokens.push_back(make_token(TokenType::OR, "||", start_line, start_col));
                else { add_error("Unexpected character '|' (did you mean '||'?).", start_line, start_col); }
                break;
            case '+':
                if (match('+')) tokens.push_back(make_token(TokenType::PLUS_PLUS, "++", start_line, start_col));
                else if (match('=')) tokens.push_back(make_token(TokenType::PLUS_ASSIGN, "+=", start_line, start_col));
                else tokens.push_back(make_token(TokenType::PLUS, "+", start_line, start_col));
                break;
            case '-':
                if (match('-')) tokens.push_back(make_token(TokenType::MINUS_MINUS, "--", start_line, start_col));
                else if (match('=')) tokens.push_back(make_token(TokenType::MINUS_ASSIGN, "-=", start_line, start_col));
                else tokens.push_back(make_token(TokenType::MINUS, "-", start_line, start_col));
                break;
            case '*':
                if (match('=')) tokens.push_back(make_token(TokenType::STAR_ASSIGN, "*=", start_line, start_col));
                else tokens.push_back(make_token(TokenType::STAR, "*", start_line, start_col));
                break;
            case '/':
                if (match('=')) tokens.push_back(make_token(TokenType::SLASH_ASSIGN, "/=", start_line, start_col));
                else tokens.push_back(make_token(TokenType::SLASH, "/", start_line, start_col));
                break;
            case '%':
                tokens.push_back(make_token(TokenType::PERCENT, "%", start_line, start_col));
                break;

            default: {
                std::string msg = "Unexpected character '";
                msg += c;
                msg += "'.";
                add_error(msg, start_line, start_col);
                break;
            }
        }
    }

    tokens.push_back(make_token(TokenType::END_OF_FILE, "", line_, col_));
    return tokens;
}

} // namespace novac
