#ifndef MMC_CODEGEN_H
#define MMC_CODEGEN_H

#include "mmc_types.h"
#include "mmc_lexer.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Builder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Verifier.h>
#include <memory>
#include <map>
#include <stdexcept>

namespace mmc {

class CodeGen {
private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
    std::map<std::string, llvm::Value*> namedValues;
    std::map<std::string, llvm::Function*> functionDecls;
    
    llvm::Value* logError(const std::string& msg) {
        fprintf(stderr, "Error: %s\n", msg.c_str());
        return nullptr;
    }
    
    llvm::Type* getLLVMType(const std::string& mmcType) {
        if (mmcType == "int" || mmcType == "အတည်") {
            return llvm::Type::getInt32Ty(*context);
        } else if (mmcType == "float" || mmcType == "ဒစ်") {
            return llvm::Type::getFloatTy(*context);
        }
        return llvm::Type::getInt32Ty(*context); // default
    }

public:
    CodeGen() 
        : context(std::make_unique<llvm::LLVMContext>()),
          module(std::make_unique<llvm::Module>("mmc_module", *context)),
          builder(std::make_unique<llvm::IRBuilder<>>(*context)) {
        
        // Declare external GGML functions
        declareGGMLFunctions();
    }
    
    void declareGGMLFunctions() {
        llvm::LLVMContext& ctx = *context;
        
        // declare i8* @ggml_load_model(i8*)
        auto loadModelType = llvm::FunctionType::get(
            llvm::Type::getInt8PtrTy(ctx),
            {llvm::Type::getInt8PtrTy(ctx)},
            false
        );
        llvm::Function::Create(loadModelType, llvm::Function::ExternalLinkage, "ggml_load_model", module.get());
        
        // declare i8* @ggml_infer(i8*, i8*)
        auto inferType = llvm::FunctionType::get(
            llvm::Type::getInt8PtrTy(ctx),
            {llvm::Type::getInt8PtrTy(ctx), llvm::Type::getInt8PtrTy(ctx)},
            false
        );
        llvm::Function::Create(inferType, llvm::Function::ExternalLinkage, "ggml_infer", module.get());
        
        // declare void @printf(i8*, ...)
        auto printfType = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(ctx),
            {llvm::Type::getInt8PtrTy(ctx)},
            true
        );
        llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", module.get());
    }
    
    llvm::Module* generate(const std::vector<Token>& tokens) {
        // Simple code generation for demonstration
        // In full implementation, this would parse AST and generate IR
        
        // Create main function
        llvm::FunctionType* mainType = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*context),
            {},
            false
        );
        
        llvm::Function* mainFunc = llvm::Function::Create(
            mainType,
            llvm::Function::ExternalLinkage,
            "main",
            module.get()
        );
        
        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", mainFunc);
        builder->SetInsertPoint(bb);
        
        // Generate simple hello world for now
        llvm::Value* helloStr = builder->CreateGlobalStringPtr("Hello from MMC!\n");
        builder->CreateCall(
            module->getFunction("printf"),
            {helloStr}
        );
        
        // Return 0
        builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
        
        // Verify module
        if (llvm::verifyModule(*module, &llvm::errs())) {
            throw std::runtime_error("Module verification failed");
        }
        
        return module.get();
    }
    
    std::string getIR() {
        std::string ir;
        llvm::raw_string_ostream os(ir);
        module->print(os, nullptr);
        return ir;
    }
    
    llvm::LLVMContext& getContext() { return *context; }
    llvm::Module& getModule() { return *module; }
};

} // namespace mmc

#endif // MMC_CODEGEN_H
