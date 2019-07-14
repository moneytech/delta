#include "driver.h"
#include <cstdio>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>
#pragma warning(push, 0)
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/ErrorOr.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#pragma warning(pop)
#include "clang.h"
#include "../ast/ast-printer.h"
#include "../ast/module.h"
#include "../irgen/irgen.h"
#include "../package-manager/manifest.h"
#include "../package-manager/package-manager.h"
#include "../parser/parse.h"
#include "../sema/typecheck.h"
#include "../support/utility.h"

#ifdef _MSC_VER
#define popen _popen
#define pclose _pclose
#define WEXITSTATUS(x) x
#endif

using namespace delta;

static int exec(const char* command, std::string& output) {
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        ABORT("failed to execute '" << command << "'");
    }

    try {
        char buffer[128];
        while (fgets(buffer, sizeof buffer, pipe)) {
            output += buffer;
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }

    int status = pclose(pipe);
    return WEXITSTATUS(status);
}

bool delta::checkFlag(llvm::StringRef flag, std::vector<llvm::StringRef>& args) {
    const auto it = std::find(args.begin(), args.end(), flag);
    const bool contains = it != args.end();
    if (contains) args.erase(it);
    return contains;
}

static std::vector<std::string> collectStringOptionValues(llvm::StringRef flagPrefix, std::vector<llvm::StringRef>& args) {
    std::vector<std::string> values;
    for (auto arg = args.begin(); arg != args.end();) {
        if (arg->startswith(flagPrefix)) {
            values.push_back(arg->substr(flagPrefix.size()));
            arg = args.erase(arg);
        } else {
            ++arg;
        }
    }
    return values;
}

static std::string collectStringOptionValue(llvm::StringRef flagPrefix, std::vector<llvm::StringRef>& args) {
    auto values = collectStringOptionValues(flagPrefix, args);
    if (values.empty()) {
        return "";
    } else {
        return std::move(values.back());
    }
}

static void addHeaderSearchPathsFromEnvVar(std::vector<std::string>& importSearchPaths, const char* name) {
    if (const char* pathList = std::getenv(name)) {
        llvm::SmallVector<llvm::StringRef, 16> paths;
        llvm::StringRef(pathList).split(paths, llvm::sys::EnvPathSeparator, -1, false);

        for (llvm::StringRef path : paths) {
            importSearchPaths.push_back(path);
        }
    }
}

static void addHeaderSearchPathsFromCCompilerOutput(std::vector<std::string>& importSearchPaths) {
    auto compilerPath = getCCompilerPath();
    if (compilerPath.empty()) return;

    if (llvm::StringRef(compilerPath).endswith_lower("cl.exe")) {
        addHeaderSearchPathsFromEnvVar(importSearchPaths, "INCLUDE");
    } else {
        std::string command = "echo | " + compilerPath + " -E -v - 2>&1 | grep '^ /'";
        std::string output;
        exec(command.c_str(), output);

        llvm::SmallVector<llvm::StringRef, 8> lines;
        llvm::SplitString(output, lines, "\n");

        for (auto line : lines) {
            auto path = line.trim();
            if (llvm::sys::fs::is_directory(path)) {
                importSearchPaths.push_back(path);
            }
        }
    }
}

static void addPredefinedImportSearchPaths(std::vector<std::string>& importSearchPaths, llvm::ArrayRef<std::string> inputFiles) {
    llvm::StringSet<> relativeImportSearchPaths;

    for (llvm::StringRef filePath : inputFiles) {
        auto directoryPath = llvm::sys::path::parent_path(filePath);
        if (directoryPath.empty()) directoryPath = ".";
        relativeImportSearchPaths.insert(directoryPath);
    }

    for (auto& keyValue : relativeImportSearchPaths) {
        importSearchPaths.push_back(keyValue.getKey());
    }

    importSearchPaths.push_back(DELTA_ROOT_DIR);
    importSearchPaths.push_back(CLANG_BUILTIN_INCLUDE_PATH);
    addHeaderSearchPathsFromCCompilerOutput(importSearchPaths);
    importSearchPaths.push_back("/usr/include");
    importSearchPaths.push_back("/usr/local/include");
    addHeaderSearchPathsFromEnvVar(importSearchPaths, "CPATH");
    addHeaderSearchPathsFromEnvVar(importSearchPaths, "C_INCLUDE_PATH");
}

static void emitMachineCode(llvm::Module& module, llvm::StringRef fileName, llvm::TargetMachine::CodeGenFileType fileType, llvm::Reloc::Model relocModel) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    const std::string& targetTriple = triple.str();
    module.setTargetTriple(targetTriple);

    std::string errorMessage;
    auto* target = llvm::TargetRegistry::lookupTarget(targetTriple, errorMessage);
    if (!target) ABORT(errorMessage);

    llvm::TargetOptions options;
    auto* targetMachine = target->createTargetMachine(targetTriple, "generic", "", options, relocModel);
    module.setDataLayout(targetMachine->createDataLayout());

    std::error_code error;
    llvm::raw_fd_ostream file(fileName, error, llvm::sys::fs::F_None);
    if (error) ABORT(error.message());

    llvm::legacy::PassManager passManager;
    if (targetMachine->addPassesToEmitFile(passManager, file, nullptr, fileType)) {
        ABORT("TargetMachine can't emit a file of this type");
    }

    passManager.run(module);
    file.flush();
}

static void emitLLVMBitcode(const llvm::Module& module, llvm::StringRef fileName) {
    std::error_code error;
    llvm::raw_fd_ostream file(fileName, error, llvm::sys::fs::F_None);
    if (error) ABORT(error.message());
    llvm::WriteBitcodeToFile(module, file);
    file.flush();
}

int delta::buildPackage(llvm::StringRef packageRoot, const char* argv0, std::vector<llvm::StringRef>& args, bool run) {
    auto manifestPath = (packageRoot + "/" + PackageManifest::manifestFileName).str();
    PackageManifest manifest(packageRoot);
    fetchDependencies(packageRoot);

    for (auto& targetRootDir : manifest.getTargetRootDirectories()) {
        llvm::StringRef outputFileName;
        if (manifest.isMultiTarget() || manifest.getPackageName().empty()) {
            outputFileName = llvm::sys::path::filename(targetRootDir);
        } else {
            outputFileName = manifest.getPackageName();
        }
        // TODO: Add support for library packages.
        int exitStatus = buildExecutable(getSourceFiles(targetRootDir, manifestPath), &manifest, argv0, args, manifest.getOutputDirectory(),
                                         outputFileName, run);
        if (exitStatus != 0) return exitStatus;
    }

    return 0;
}

int delta::buildExecutable(llvm::ArrayRef<std::string> files, const PackageManifest* manifest, const char* argv0,
                           std::vector<llvm::StringRef>& args, llvm::StringRef outputDirectory, llvm::StringRef outputFileName, bool run) {
    CompileOptions options;
    bool parse = checkFlag("-parse", args);
    bool typecheck = checkFlag("-typecheck", args);
    bool compileOnly = checkFlag("-c", args);
    bool printAST = checkFlag("-print-ast", args);
    bool printIR = checkFlag("-print-ir", args);
    bool emitAssembly = checkFlag("-emit-assembly", args) || checkFlag("-S", args);
    bool emitLLVMBitcode = checkFlag("-emit-llvm-bitcode", args);
    bool emitPositionIndependentCode = checkFlag("-fPIC", args);
    if (checkFlag("-w", args)) warningMode = WarningMode::Suppress;
    if (checkFlag("-Werror", args)) warningMode = WarningMode::TreatAsErrors;
    options.disabledWarnings = collectStringOptionValues("-Wno-", args);
    options.defines = collectStringOptionValues("-D", args);
#ifdef _WIN32
    options.defines.push_back("Windows");
#endif
    options.importSearchPaths = collectStringOptionValues("-I", args);
    options.frameworkSearchPaths = collectStringOptionValues("-F", args);
    auto specifiedOutputFileName = collectStringOptionValue("-o", args);
    if (!specifiedOutputFileName.empty()) {
        outputFileName = specifiedOutputFileName;
    }

    for (llvm::StringRef arg : args) {
        if (arg.startswith("-")) {
            ABORT("unsupported option '" << arg << "'");
        }
    }

    if (files.empty()) {
        ABORT("no input files");
    }

    addPredefinedImportSearchPaths(options.importSearchPaths, files);
    Module module("main");

    for (llvm::StringRef filePath : files) {
        Parser parser(filePath, module, options);
        parser.parse();
    }

    if (printAST) {
        ASTPrinter astPrinter(std::cout);
        astPrinter.printModule(module);
        return 0;
    }

    if (parse) return 0;

    Typechecker typechecker(options);
    for (auto& importedModule : module.getImportedModules()) {
        typechecker.typecheckModule(*importedModule, nullptr);
    }
    typechecker.typecheckModule(module, manifest);

    bool treatAsLibrary = !module.getSymbolTable().contains("main") && !run;
    if (treatAsLibrary) {
        compileOnly = true;
    }

    if (typecheck) return 0;

    IRGenerator irGenerator;

    for (auto* module : Module::getAllImportedModules()) {
        irGenerator.codegenModule(*module);
    }

    auto& mainModule = irGenerator.codegenModule(module);

    if (printIR) {
        mainModule.setModuleIdentifier("");
        mainModule.setSourceFileName("");
        mainModule.print(llvm::outs(), nullptr);
        return 0;
    }

    llvm::Module linkedModule("", irGenerator.getLLVMContext());
    llvm::Linker linker(linkedModule);

    for (auto& module : irGenerator.getGeneratedModules()) {
        bool error = linker.linkInModule(std::unique_ptr<llvm::Module>(module));
        if (error) ABORT("LLVM module linking failed");
    }

    if (emitLLVMBitcode) {
        ::emitLLVMBitcode(linkedModule, "output.bc");
        return 0;
    }

    auto ccPath = getCCompilerPath();
    bool msvc = llvm::StringRef(ccPath).endswith_lower(".exe");

    llvm::SmallString<128> temporaryOutputFilePath;
    auto* outputFileExtension = emitAssembly ? "s" : msvc ? "obj" : "o";
    if (auto error = llvm::sys::fs::createTemporaryFile("delta", outputFileExtension, temporaryOutputFilePath)) {
        ABORT(error.message());
    }

    auto fileType = emitAssembly ? llvm::TargetMachine::CGFT_AssemblyFile : llvm::TargetMachine::CGFT_ObjectFile;
    if (msvc) emitPositionIndependentCode = true;
    auto relocModel = emitPositionIndependentCode ? llvm::Reloc::Model::PIC_ : llvm::Reloc::Model::Static;
    emitMachineCode(linkedModule, temporaryOutputFilePath, fileType, relocModel);

    if (!outputDirectory.empty()) {
        auto error = llvm::sys::fs::create_directories(outputDirectory);
        if (error) ABORT(error.message());
    }

    if (compileOnly || emitAssembly) {
        llvm::SmallString<128> outputFilePath = outputDirectory;
        llvm::sys::path::append(outputFilePath, llvm::Twine("output.") + outputFileExtension);
        renameFile(temporaryOutputFilePath, outputFilePath);
        return 0;
    }

    // Link the output.

    llvm::SmallString<128> temporaryExecutablePath;
    const char* executableNamePattern = msvc ? "delta-%%%%%%%%.exe" : "delta-%%%%%%%%.out";

    if (auto error = llvm::sys::fs::createUniqueFile(executableNamePattern, temporaryExecutablePath)) {
        ABORT(error.message());
    }

    std::vector<const char*> ccArgs = {
        msvc ? ccPath.c_str() : argv0,
        temporaryOutputFilePath.c_str(),
    };

    std::string outputPathFlag = ((msvc ? "-Fe" : "-o") + temporaryExecutablePath).str();
    ccArgs.push_back(outputPathFlag.c_str());

    if (msvc) {
        ccArgs.push_back("-link");
        ccArgs.push_back("-DEBUG");
        ccArgs.push_back("legacy_stdio_definitions.lib");
        ccArgs.push_back("ucrt.lib");
        ccArgs.push_back("msvcrt.lib");
    }

    std::vector<llvm::StringRef> ccArgStringRefs(ccArgs.begin(), ccArgs.end());
    int ccExitStatus = msvc ? llvm::sys::ExecuteAndWait(ccArgs[0], ccArgStringRefs) : invokeClang(ccArgs);
    llvm::sys::fs::remove(temporaryOutputFilePath);
    if (ccExitStatus != 0) return ccExitStatus;

    if (run) {
        if (auto error = llvm::sys::fs::make_absolute(temporaryExecutablePath)) {
            ABORT("couldn't make an absolute path: " << error.message());
        }

        std::string command = (temporaryExecutablePath + " 2>&1").str();
        std::string output;
        int executableExitStatus = exec(command.c_str(), output);
        llvm::outs() << output;
        llvm::sys::fs::remove(temporaryExecutablePath);

        if (msvc) {
            auto path = temporaryExecutablePath;
            llvm::sys::path::replace_extension(path, "ilk");
            llvm::sys::fs::remove(path);
            llvm::sys::path::replace_extension(path, "pdb");
            llvm::sys::fs::remove(path);
        }

        return executableExitStatus;
    }

    llvm::SmallString<128> outputPathPrefix = outputDirectory;
    if (!outputPathPrefix.empty()) {
        outputPathPrefix.append(llvm::sys::path::get_separator());
    }

    if (outputFileName.empty()) {
        outputFileName = msvc ? "a.exe" : "a.out";
    }

    renameFile(temporaryExecutablePath, outputPathPrefix + outputFileName);

    if (msvc) {
        auto path = temporaryExecutablePath;
        auto outputPath = outputPathPrefix;
        outputPath += outputFileName;

        llvm::sys::path::replace_extension(path, "ilk");
        llvm::sys::path::replace_extension(outputPath, "ilk");
        renameFile(path, outputPath);

        llvm::sys::path::replace_extension(path, "pdb");
        llvm::sys::path::replace_extension(outputPath, "pdb");
        renameFile(path, outputPath);
    }

    return 0;
}
