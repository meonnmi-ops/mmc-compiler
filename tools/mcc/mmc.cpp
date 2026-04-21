#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/Host.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/CodeGen/CommandFlags.h>

#include "mmc_lexer.h"
#include "mmc_codegen.h"

void printVersion() {
    std::cout << "MMC Compiler v1.0.0 (Native ARM64 AI Compiler)\n";
    std::cout << "Based on LLVM 18.x\n";
    std::cout << "Target: aarch64-linux-android\n";
    std::cout << "Supports: Myanmar Programming Language with AI inference\n";
}

void printHelp(const char* progName) {
    std::cout << "Usage: " << progName << " [options] <input.mmc>\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o <output>     Output file name\n";
    std::cout << "  -S              Output LLVM IR only\n";
    std::cout << "  -c              Compile to object file only\n";
    std::cout << "  --version       Show version\n";
    std::cout << "  --help          Show this help\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << progName << " -o hello hello.mmc\n";
    std::cout << "  " << progName << " -S -o hello.ll hello.mmc\n";
}

std::string readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool writeToFile(const std::string& filename, const std::string& content) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot write to " << filename << "\n";
        return false;
    }
    file << content;
    return true;
}

bool writeBinary(const std::string& filename, const std::vector<char>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot write to " << filename << "\n";
        return false;
    }
    file.write(data.data(), data.size());
    return true;
}

int main(int argc, char** argv) {
    // Initialize LLVM
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    
    std::string inputFile;
    std::string outputFile;
    bool outputIR = false;
    bool outputObject = false;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--version") {
            printVersion();
            return 0;
        }
        else if (arg == "--help") {
            printHelp(argv[0]);
            return 0;
        }
        else if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        }
        else if (arg == "-S") {
            outputIR = true;
        }
        else if (arg == "-c") {
            outputObject = true;
        }
        else if (arg[0] != '-') {
            inputFile = arg;
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            printHelp(argv[0]);
            return 1;
        }
    }
    
    if (inputFile.empty()) {
        std::cerr << "Error: No input file specified\n";
        printHelp(argv[0]);
        return 1;
    }
    
    if (outputFile.empty()) {
        // Default output name
        size_t dotPos = inputFile.rfind('.');
        if (dotPos != std::string::npos) {
            outputFile = inputFile.substr(0, dotPos);
        } else {
            outputFile = inputFile + ".out";
        }
    }
    
    try {
        // Read source file
        std::cout << "📖 Reading: " << inputFile << "\n";
        std::string source = readFile(inputFile);
        
        // Tokenize
        std::cout << "🔍 Tokenizing...\n";
        mmc::Lexer lexer(source);
        std::vector<mmc::Token> tokens = lexer.tokenize();
        
        std::cout << "✓ Generated " << tokens.size() << " tokens\n";
        
        // Generate LLVM IR
        std::cout << "⚙️  Generating LLVM IR...\n";
        mmc::CodeGen codegen;
        llvm::Module* module = codegen.generate(tokens);
        
        // Output based on flags
        if (outputIR) {
            std::string ir = codegen.getIR();
            std::cout << "📝 Writing IR to: " << outputFile << "\n";
            if (!writeToFile(outputFile, ir)) {
                return 1;
            }
            std::cout << "✅ Done! IR output written.\n";
        }
        else {
            // Generate object file or executable
            std::cout << "🎯 Compiling to binary...\n";
            
            // Get target machine
            std::string targetTriple = llvm::sys::getDefaultTargetTriple();
            std::string error;
            const llvm::Target* target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
            
            if (!target) {
                std::cerr << "Error: " << error << "\n";
                return 1;
            }
            
            llvm::TargetOptions opt;
            auto RM = std::optional<llvm::Reloc::Model>();
            auto TM = std::unique_ptr<llvm::TargetMachine>(
                target->createTargetMachine(targetTriple, "generic", "", opt, RM)
            );
            
            module->setDataLayout(TM->createDataLayout());
            module->setTargetTriple(targetTriple);
            
            // Write object file
            std::error_code EC;
            llvm::raw_fd_ostream dest(outputFile, EC, llvm::sys::fs::OF_None);
            
            if (EC) {
                std::cerr << "Error: " << EC.message() << "\n";
                return 1;
            }
            
            llvm::legacy::PassManager pass;
            if (TM->addPassesToEmitFile(pass, dest, nullptr, llvm::CGFT_ObjectFile)) {
                std::cerr << "Error: TargetMachine can't emit object files\n";
                return 1;
            }
            
            pass.run(*module);
            dest.flush();
            
            std::cout << "✅ Binary compiled: " << outputFile << "\n";
            
            // Make executable
            std::string chmodCmd = "chmod +x " + outputFile;
            system(chmodCmd.c_str());
            
            std::cout << "🚀 Ready to run: ./" << outputFile << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
