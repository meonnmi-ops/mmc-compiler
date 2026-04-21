/**
 * MMC Compiler - Simple Native Version
 * Compiles MMC source directly to ARM64 binary using Clang
 * No LLVM dependency - pure C implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_CODE_SIZE 65536
#define MAX_TOKENS 10000

// Simple token types
typedef enum {
    TOK_PRINT,      // ပုံနှိပ်
    TOK_INT,        // အတည်
    TOK_FLOAT,      // ဒစ်
    TOK_IF,         // အကယ်၍
    TOK_FOR,        // အတွက်
    TOK_FUNC,       // အဓိပ္ပာယ်
    TOK_RETURN,     // ပြန်ပေး
    TOK_STRING,
    TOK_NUMBER,
    TOK_IDENT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_SEMI,
    TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char value[256];
} Token;

static Token tokens[MAX_TOKENS];
static int token_count = 0;
static char generated_code[MAX_CODE_SIZE];
static int code_pos = 0;

// Forward declarations
void emit(const char* fmt, ...);
int parse(const char* source);
int compile_to_c(const char* input, const char* output);

void emit(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    code_pos += vsprintf(generated_code + code_pos, fmt, args);
    va_end(args);
}

int tokenize(const char* source) {
    token_count = 0;
    int i = 0;
    int line = 1;
    
    while (source[i] != '\0') {
        // Skip whitespace
        while (isspace(source[i])) {
            if (source[i] == '\n') line++;
            i++;
        }
        
        if (source[i] == '\0') break;
        
        // Skip comments
        if (source[i] == '#' || (source[i] == '/' && source[i+1] == '/')) {
            while (source[i] != '\n' && source[i] != '\0') i++;
            continue;
        }
        
        // String literal
        if (source[i] == '"') {
            i++;
            int start = i;
            while (source[i] != '"' && source[i] != '\0') i++;
            int len = i - start;
            if (len > 250) len = 250;
            tokens[token_count].type = TOK_STRING;
            strncpy(tokens[token_count].value, source + start, len);
            tokens[token_count].value[len] = '\0';
            token_count++;
            if (source[i] == '"') i++;
            continue;
        }
        
        // Number
        if (isdigit(source[i])) {
            int start = i;
            while (isdigit(source[i])) i++;
            int len = i - start;
            if (len > 20) len = 20;
            tokens[token_count].type = TOK_NUMBER;
            strncpy(tokens[token_count].value, source + start, len);
            tokens[token_count].value[len] = '\0';
            token_count++;
            continue;
        }
        
        // Punctuation
        if (source[i] == '(') {
            tokens[token_count].type = TOK_LPAREN;
            strcpy(tokens[token_count].value, "(");
            token_count++;
            i++;
            continue;
        }
        if (source[i] == ')') {
            tokens[token_count].type = TOK_RPAREN;
            strcpy(tokens[token_count].value, ")");
            token_count++;
            i++;
            continue;
        }
        if (source[i] == '{') {
            tokens[token_count].type = TOK_LBRACE;
            strcpy(tokens[token_count].value, "{");
            token_count++;
            i++;
            continue;
        }
        if (source[i] == '}') {
            tokens[token_count].type = TOK_RBRACE;
            strcpy(tokens[token_count].value, "}");
            token_count++;
            i++;
            continue;
        }
        if (source[i] == ';') {
            tokens[token_count].type = TOK_SEMI;
            strcpy(tokens[token_count].value, ";");
            token_count++;
            i++;
            continue;
        }
        
        // Myanmar keywords or identifiers
        if ((unsigned char)source[i] >= 0x80 || isalpha(source[i])) {
            int start = i;
            while (((unsigned char)source[i] >= 0x80 || isalnum(source[i]) || source[i] == '_') && source[i] != '\0') {
                i++;
            }
            int len = i - start;
            if (len > 250) len = 250;
            
            char word[256];
            strncpy(word, source + start, len);
            word[len] = '\0';
            
            // Check keywords
            if (strcmp(word, "ပုံနှိပ်") == 0) {
                tokens[token_count].type = TOK_PRINT;
            } else if (strcmp(word, "အတည်") == 0) {
                tokens[token_count].type = TOK_INT;
            } else if (strcmp(word, "ဒစ်") == 0) {
                tokens[token_count].type = TOK_FLOAT;
            } else if (strcmp(word, "အကယ်၍") == 0) {
                tokens[token_count].type = TOK_IF;
            } else if (strcmp(word, "အတွက်") == 0) {
                tokens[token_count].type = TOK_FOR;
            } else if (strcmp(word, "အဓိပ္ပာယ်") == 0) {
                tokens[token_count].type = TOK_FUNC;
            } else if (strcmp(word, "ပြန်ပေး") == 0) {
                tokens[token_count].type = TOK_RETURN;
            } else {
                tokens[token_count].type = TOK_IDENT;
            }
            
            strcpy(tokens[token_count].value, word);
            token_count++;
            continue;
        }
        
        // Unknown character - skip
        i++;
    }
    
    tokens[token_count].type = TOK_EOF;
    strcpy(tokens[token_count].value, "");
    token_count++;
    
    return token_count;
}

int generate_c_code() {
    code_pos = 0;
    
    // Generate C header
    emit("#include <stdio.h>\n");
    emit("#include <stdlib.h>\n");
    emit("#include <string.h>\n\n");
    
    // Generate main function
    emit("int main() {\n");
    
    int i = 0;
    while (i < token_count && tokens[i].type != TOK_EOF) {
        // Function definition: အဓိပ္ပာယ် main()
        if (tokens[i].type == TOK_FUNC) {
            i++; // skip function name
            if (i < token_count && strcmp(tokens[i].value, "main") == 0) {
                i++; // skip (
                while (i < token_count && tokens[i].type != TOK_RPAREN) i++;
                i++; // skip )
                if (i < token_count && tokens[i].type == TOK_LBRACE) {
                    i++; // skip {
                    // Already in main, continue
                }
            }
            continue;
        }
        
        // Print statement: ပုံနှိပ်("...")
        if (tokens[i].type == TOK_PRINT) {
            i++; // skip ပုံနှိပ်
            if (i < token_count && tokens[i].type == TOK_LPAREN) {
                i++; // skip (
                if (i < token_count && tokens[i].type == TOK_STRING) {
                    emit("    printf(\"%%s\\n\", \"%s\");\n", tokens[i].value);
                    i++;
                } else if (i < token_count && tokens[i].type == TOK_NUMBER) {
                    emit("    printf(\"%%d\\n\", %s);\n", tokens[i].value);
                    i++;
                }
                if (i < token_count && tokens[i].type == TOK_RPAREN) i++;
                if (i < token_count && tokens[i].type == TOK_SEMI) i++;
            }
            continue;
        }
        
        // Return statement: ပြန်ပေး 0
        if (tokens[i].type == TOK_RETURN) {
            i++; // skip ပြန်ပေး
            if (i < token_count && tokens[i].type == TOK_NUMBER) {
                emit("    return %s;\n", tokens[i].value);
                i++;
            } else {
                emit("    return 0;\n");
            }
            if (i < token_count && tokens[i].type == TOK_SEMI) i++;
            continue;
        }
        
        // Skip closing brace
        if (tokens[i].type == TOK_RBRACE) {
            i++;
            continue;
        }
        
        // Skip unknown tokens
        i++;
    }
    
    // Close main function
    emit("    return 0;\n");
    emit("}\n");
    
    return code_pos;
}

int compile_to_binary(const char* input_file, const char* output_file) {
    // Read source file
    FILE* f = fopen(input_file, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s\n", input_file);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* source = (char*)malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);
    
    printf("📖 Read %ld bytes from %s\n", size, input_file);
    
    // Tokenize
    int count = tokenize(source);
    printf("🔍 Generated %d tokens\n", count);
    
    // Generate C code
    int code_len = generate_c_code();
    printf("⚙️  Generated %d bytes of C code\n", code_len);
    
    // Write intermediate C file
    char temp_c[] = "/data/data/com.termux/files/home/mmc-compiler/bin/temp.c";
    FILE* cf = fopen(temp_c, "w");
    if (cf) {
        fwrite(generated_code, 1, code_len, cf);
        fclose(cf);
        printf("📝 Wrote intermediate C file\n");
    }
    
    // Compile with clang
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "clang -o %s %s -O2", output_file, temp_c);
    printf("🔨 Compiling with clang: %s\n", cmd);
    
    int result = system(cmd);
    if (result == 0) {
        // Make executable
        chmod(output_file, 0755);
        printf("✅ Binary created: %s\n", output_file);
        
        // Clean up temp file
        unlink(temp_c);
    } else {
        fprintf(stderr, "❌ Compilation failed\n");
        free(source);
        return 1;
    }
    
    free(source);
    return 0;
}

void print_version() {
    printf("MMC Compiler v1.0.0 (Native ARM64)\n");
    printf("Simple C-based compiler for Myanmar Programming Language\n");
    printf("Target: aarch64-linux-android (Termux)\n");
}

void print_help(const char* prog) {
    printf("Usage: %s [options] <input.mmc>\n\n", prog);
    printf("Options:\n");
    printf("  -o <output>  Output binary name\n");
    printf("  --version    Show version\n");
    printf("  --help       Show help\n\n");
    printf("Example:\n");
    printf("  %s -o hello examples/hello.mmc\n", prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }
    
    const char* input_file = NULL;
    const char* output_file = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        }
    }
    
    if (!input_file) {
        fprintf(stderr, "Error: No input file specified\n");
        print_help(argv[0]);
        return 1;
    }
    
    if (!output_file) {
        // Default output name
        output_file = strdup(input_file);
        char* dot = strrchr((char*)output_file, '.');
        if (dot) *dot = '\0';
    }
    
    printf("🚀 MMC Compiler starting...\n");
    printf("📥 Input: %s\n", input_file);
    printf("📤 Output: %s\n", output_file);
    
    int result = compile_to_binary(input_file, output_file);
    
    if (result == 0) {
        printf("\n🎉 Success! Run with: ./%s\n", output_file);
    }
    
    return result;
}
