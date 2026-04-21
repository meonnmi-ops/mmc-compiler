#ifndef MMC_LEXER_H
#define MMC_LEXER_H

#include "mmc_types.h"
#include <string>
#include <vector>
#include <cctype>

namespace mmc {

class Lexer {
private:
    std::string source;
    size_t pos;
    int line;
    int column;
    
    char current() const {
        return (pos < source.length()) ? source[pos] : '\0';
    }
    
    char peek() const {
        return (pos + 1 < source.length()) ? source[pos + 1] : '\0';
    }
    
    void advance() {
        if (current() == '\n') {
            line++;
            column = 0;
        } else {
            column++;
        }
        pos++;
    }
    
    void skipWhitespace() {
        while (std::isspace(current())) {
            advance();
        }
    }
    
    void skipComment() {
        if (current() == '#' || (current() == '/' && peek() == '/')) {
            while (current() != '\n' && current() != '\0') {
                advance();
            }
        }
    }
    
    std::string readString() {
        std::string result;
        advance(); // skip opening quote
        while (current() != '"' && current() != '\0') {
            result += current();
            advance();
        }
        if (current() == '"') advance(); // skip closing quote
        return result;
    }
    
    std::string readNumber() {
        std::string result;
        bool hasDot = false;
        while (std::isdigit(current()) || (current() == '.' && !hasDot)) {
            if (current() == '.') hasDot = true;
            result += current();
            advance();
        }
        return result;
    }
    
    std::string readIdentifier() {
        std::string result;
        // Myanmar Unicode characters or ASCII
        while (std::isalnum(current()) || 
               (static_cast<unsigned char>(current()) >= 0x80) || 
               current() == '_') {
            result += current();
            advance();
        }
        return result;
    }

public:
    Lexer(const std::string& src) : source(src), pos(0), line(1), column(0) {}
    
    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        
        while (current() != '\0') {
            skipWhitespace();
            skipComment();
            skipWhitespace();
            
            if (current() == '\0') break;
            
            int startLine = line;
            int startCol = column;
            
            // String literal
            if (current() == '"') {
                std::string value = readString();
                tokens.emplace_back(TokenType::STRING_LITERAL, value, startLine, startCol);
            }
            // Number
            else if (std::isdigit(current())) {
                std::string value = readNumber();
                TokenType type = (value.find('.') != std::string::npos) 
                    ? TokenType::NUMBER_FLOAT 
                    : TokenType::NUMBER_INT;
                tokens.emplace_back(type, value, startLine, startCol);
            }
            // Identifier or Keyword (including Myanmar)
            else if (std::isalpha(current()) || static_cast<unsigned char>(current()) >= 0x80) {
                std::string value = readIdentifier();
                TokenType type = keywordToType(value);
                tokens.emplace_back(type, value, startLine, startCol);
            }
            // Operators
            else if (current() == '+') {
                tokens.emplace_back(TokenType::OP_PLUS, "+", startLine, startCol);
                advance();
            }
            else if (current() == '-') {
                tokens.emplace_back(TokenType::OP_MINUS, "-", startLine, startCol);
                advance();
            }
            else if (current() == '*') {
                tokens.emplace_back(TokenType::OP_MULTIPLY, "*", startLine, startCol);
                advance();
            }
            else if (current() == '/') {
                tokens.emplace_back(TokenType::OP_DIVIDE, "/", startLine, startCol);
                advance();
            }
            else if (current() == '=' && peek() == '=') {
                tokens.emplace_back(TokenType::OP_EQUAL, "==", startLine, startCol);
                advance(); advance();
            }
            else if (current() == '=' ) {
                tokens.emplace_back(TokenType::OP_ASSIGN, "=", startLine, startCol);
                advance();
            }
            else if (current() == '<') {
                tokens.emplace_back(TokenType::OP_LESS, "<", startLine, startCol);
                advance();
            }
            else if (current() == '>') {
                tokens.emplace_back(TokenType::OP_GREATER, ">", startLine, startCol);
                advance();
            }
            else if (current() == '(') {
                tokens.emplace_back(TokenType::DELIM_LPAREN, "(", startLine, startCol);
                advance();
            }
            else if (current() == ')') {
                tokens.emplace_back(TokenType::DELIM_RPAREN, ")", startLine, startCol);
                advance();
            }
            else if (current() == '{') {
                tokens.emplace_back(TokenType::DELIM_LBRACE, "{", startLine, startCol);
                advance();
            }
            else if (current() == '}') {
                tokens.emplace_back(TokenType::DELIM_RBRACE, "}", startLine, startCol);
                advance();
            }
            else if (current() == ',') {
                tokens.emplace_back(TokenType::DELIM_COMMA, ",", startLine, startCol);
                advance();
            }
            else if (current() == ';') {
                tokens.emplace_back(TokenType::DELIM_SEMICOLON, ";", startLine, startCol);
                advance();
            }
            else {
                // Unknown character
                tokens.emplace_back(TokenType::UNKNOWN, std::string(1, current()), startLine, startCol);
                advance();
            }
        }
        
        tokens.emplace_back(TokenType::EOF_TOKEN, "", line, column);
        return tokens;
    }
};

} // namespace mmc

#endif // MMC_LEXER_H
