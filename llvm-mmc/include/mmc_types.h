#ifndef MMC_TYPES_H
#define MMC_TYPES_H

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace mmc {

// MMC Token Types
enum class TokenType {
    // Keywords
    KEYWORD_PONNAIK,      // ပုံနှိပ် (print)
    KEYWORD_ATINE,        // အတည် (int)
    KEYWORD_DIT,          // ဒစ် (float)
    KEYWORD_AKALYI,       // အကယ်၍ (if)
    KEYWORD_ATWET,        // အတွက် (for)
    KEYWORD_AHPIPANY,     // အဓိပ္ပာယ် (function)
    KEYWORD_PYANPAY,      // ပြန်ပေး (return)
    KEYWORD_AI_SIN,       // AIစရင်း (AI load)
    KEYWORD_AI_MAY,       // AIမေး (AI ask)
    
    // Literals & Identifiers
    IDENTIFIER,
    NUMBER_INT,
    NUMBER_FLOAT,
    STRING_LITERAL,
    
    // Operators
    OP_PLUS,      // +
    OP_MINUS,     // -
    OP_MULTIPLY,  // *
    OP_DIVIDE,    // /
    OP_ASSIGN,    // =
    OP_EQUAL,     // ==
    OP_NOT_EQUAL, // !=
    OP_LESS,      // <
    OP_GREATER,   // >
    
    // Delimiters
    DELIM_LPAREN,   // (
    DELIM_RPAREN,   // )
    DELIM_LBRACE,   // {
    DELIM_RBRACE,   // }
    DELIM_COMMA,    // ,
    DELIM_SEMICOLON,// ;
    
    // Special
    EOF_TOKEN,
    UNKNOWN
};

// Token structure
struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
    
    Token(TokenType t, const std::string& v, int l, int c)
        : type(t), value(v), line(l), column(c) {}
};

// AST Node Types
enum class ASTNodeType {
    PROGRAM,
    FUNCTION_DEF,
    FUNCTION_CALL,
    VARIABLE_DECL,
    ASSIGNMENT,
    IF_STATEMENT,
    FOR_LOOP,
    RETURN_STATEMENT,
    PRINT_STATEMENT,
    AI_LOAD,
    AI_QUERY,
    BINARY_OP,
    NUMBER_LITERAL,
    STRING_LITERAL,
    IDENTIFIER
};

// Base AST Node
struct ASTNode {
    ASTNodeType type;
    std::vector<std::shared_ptr<ASTNode>> children;
    std::string value;
    std::string dataType;
    
    ASTNode(ASTNodeType t) : type(t) {}
    virtual ~ASTNode() = default;
};

// Function to convert Myanmar keywords to token types
inline TokenType keywordToType(const std::string& keyword) {
    static std::map<std::string, TokenType> keywords = {
        {"ပုံနှိပ်", TokenType::KEYWORD_PONNAIK},
        {"အတည်", TokenType::KEYWORD_ATINE},
        {"ဒစ်", TokenType::KEYWORD_DIT},
        {"အကယ်၍", TokenType::KEYWORD_AKALYI},
        {"အတွက်", TokenType::KEYWORD_ATWET},
        {"အဓိပ္ပာယ်", TokenType::KEYWORD_AHPIPANY},
        {"ပြန်ပေး", TokenType::KEYWORD_PYANPAY},
        {"AIစရင်း", TokenType::KEYWORD_AI_SIN},
        {"AIမေး", TokenType::KEYWORD_AI_MAY}
    };
    
    auto it = keywords.find(keyword);
    return (it != keywords.end()) ? it->second : TokenType::IDENTIFIER;
}

} // namespace mmc

#endif // MMC_TYPES_H
