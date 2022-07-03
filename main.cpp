#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "KaleidoscopeJIT.h"

using namespace llvm;
using namespace llvm::orc;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
    tok_eof = -1,

    // commands
    tok_def = -2,
    tok_extern = -3,

    // primary
    tok_identifier = -4,
    tok_number = -5,

    // control
    tok_if = -6,
    tok_then = -7,
    tok_else = -8,
    tok_for = -9,
    tok_in = -10,

    // operators
    tok_binary = -11,
    tok_unary = -12,

    // var definition
    tok_var = -13
};

std::string getTokName(int Tok) {
    switch (Tok) {
        case tok_eof:
            return "eof";
        case tok_def:
            return "def";
        case tok_extern:
            return "extern";
        case tok_identifier:
            return "identifier";
        case tok_number:
            return "number";
        case tok_if:
            return "if";
        case tok_then:
            return "then";
        case tok_else:
            return "else";
        case tok_for:
            return "for";
         case tok_in:
            return "in";
         case tok_binary:
            return "binary";
         case tok_unary:
            return "unary";
         case tok_var:
            return "var";
    }
    return std::string(1, (char)Tok);
}

namespace {
class PrototypeAST;
class ExprAST;
}
static LLVMContext TheContext;
static IRBuilder<> Builder(TheContext);
struct DebugInfo {
    DICompileUnit *TheCU;
    DIType *DblTy;
    std::vector<DIScope *> LexicalBlocks;

    void emitLocation(ExprAST *AST);
    DIType *getDoubleTy();
} KSDbgInfo;

struct SourceLocation {
    int Line;
    int Col;
};
static SourceLocation CurLoc;
static SourceLocation LexLoc = {1, 0};

static int advance() {
    int LastChar = getchar();

    if (LastChar == '\n' || LastChar == '\r') {
        LexLoc.Line++;
        LexLoc.Col = 0;
    } else
        LexLoc.Col++;
    return LastChar;
}

static std::string IdentifierStr; // Filled in if tok_identifier
static double NumVal;             // Filled in if tok_number

/// gettok - Return the next token from standard input.
static int gettok() {
    static int LastChar = ' ';

    // Skip any whitespace.
    while (isspace(LastChar))
        LastChar = advance();
    
    CurLoc = LexLoc;
    
    if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
        IdentifierStr = LastChar;
        while (isalnum((LastChar = advance())))
            IdentifierStr += LastChar;
        
        if (IdentifierStr == "def")
            return tok_def;
        if (IdentifierStr == "extern")
            return tok_extern;
        if (IdentifierStr == "if")
            return tok_if;
        if (IdentifierStr == "then")
            return tok_then;
        if (IdentifierStr == "else")
            return tok_else;
        if (IdentifierStr == "for")
            return tok_for;
        if (IdentifierStr == "in")
            return tok_in;
        if (IdentifierStr == "binary")
            return tok_binary;
        if (IdentifierStr == "unary")
            return tok_unary;
        if (IdentifierStr == "var")    
            return tok_var;
        return tok_identifier;
    }

    if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = advance();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }

    if (LastChar == '#') {
        do
            LastChar = advance();
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF)
            return gettok();
    }

    if (LastChar == EOF)
        return tok_eof;
    
    int ThisChar = LastChar;
    LastChar = advance();
    return ThisChar;
}

//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//
namespace {

raw_ostream &indent(raw_ostream &O, int size) {
    return O << std::string(size, ' ');
}

/// ExprAST - Base class for all expression nodes.
class ExprAST {
    SourceLocation Loc;

public:
    ExprAST(SourceLocation Loc = CurLoc) : Loc(Loc) {}
    virtual ~ExprAST() {}
    virtual Value *codegen() = 0;
    int getLine() const { return Loc.Line; }
    int getCol() const { return Loc.Col; }
    virtual raw_ostream &dump(raw_ostream &out, int ind) {
        return out << ':' << getLine() << ':' << getCol() << '\n';
    }
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
    double Val;

public:
    NumberExprAST(double Val) : Val(Val) {}
    raw_ostream &dump(raw_ostream &out, int ind) override {
        return ExprAST::dump(out << Val, ind);
    }
    Value *codegen() override;
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(SourceLocation Loc, const std::string &Name)
         : ExprAST(Loc), Name(Name) {}
    const std::string &getName() const { return Name; }
    Value *codegen() override;
    raw_ostream &dump(raw_ostream &out, int ind) override {
        return ExprAST::dump(out << Name, ind);
    }
};

/// UnaryExprAST - Expression class for a unary operator.
class UnaryExprAST : public ExprAST {
    char Opcode;
    std::unique_ptr<ExprAST> Operand;

public:
    UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
        : Opcode(Opcode), Operand(std::move(Operand)) {}    
    Value *codegen() override;
    raw_ostream &dump(raw_ostream &out, int ind) override {
        ExprAST::dump(out << "unary" << Opcode, ind);
        Operand->dump(out, ind + 1);
        return out;
    }
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(SourceLocation Loc, char Op, std::unique_ptr<ExprAST> LHS,
                  std::unique_ptr<ExprAST> RHS)
        : ExprAST(Loc), Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    Value *codegen() override;
    raw_ostream &dump(raw_ostream &out, int ind) override {
        ExprAST::dump(out << "binary" << Op, ind);
        LHS->dump(indent(out, ind) << "LHS:", ind + 1);
        RHS->dump(indent(out, ind) << "RHS:", ind + 1);
        return out;
    }
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

public:
    CallExprAST(SourceLocation Loc, const std::string &Callee,
                std::vector<std::unique_ptr<ExprAST>> Args)
        : ExprAST(Loc), Callee(Callee), Args(std::move(Args)) {}
    Value *codegen() override;
    raw_ostream &dump(raw_ostream &out, int ind) override {
        ExprAST::dump(out << "call " << Callee, ind);
        for (const auto &Arg : Args)
            Arg->dump(indent(out, ind + 1), ind + 1);
        return out;
    }
};

/// IfExprAST - Expression class for if/then/else.
class IfExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Cond, Then, Else;

public:
    IfExprAST(SourceLocation Loc, std::unique_ptr<ExprAST> Cond,
              std::unique_ptr<ExprAST> Then, std::unique_ptr<ExprAST> Else)
        : ExprAST(Loc), Cond(std::move(Cond)), Then(std::move(Then)),
          Else(std::move(Else)) {}
    Value *codegen() override;
    raw_ostream &dump(raw_ostream &out, int ind) override {
        ExprAST::dump(out << "if", ind);
        Cond->dump(indent(out, ind) << "Cond:", ind + 1);
        Then->dump(indent(out, ind) << "Then:", ind + 1);
        Else->dump(indent(out, ind) << "Else:", ind + 1);
        return out;
    }
};

/// ForExprAST - Expression class for for/in.
class ForExprAST : public ExprAST {
    std::string VarName;
    std::unique_ptr<ExprAST> Start, End, Step, Body;

public:
    ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start,
              std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
              std::unique_ptr<ExprAST> Body)
        : VarName(VarName), Start(std::move(Start)), End(std::move(End)),
          Step(std::move(Step)), Body(std::move(Body)) {}
    Value *codegen() override;
    raw_ostream &dump(raw_ostream &out, int ind) override {
        ExprAST::dump(out << "for", ind);
        Start->dump(indent(out, ind) << "Cond:", ind + 1);
        End->dump(indent(out, ind) << "End:", ind + 1);
        Step->dump(indent(out, ind) << "Step:", ind + 1);
        Body->dump(indent(out, ind) << "Body:", ind + 1);
        return out;
    }
};

/// VarExprAST - Expression class for var/in
class VarExprAST : public ExprAST {
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
    std::unique_ptr<ExprAST> Body;

public:
    VarExprAST(
        std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
        std::unique_ptr<ExprAST> Body)
        : VarNames(std::move(VarNames)), Body(std::move(Body)) {}
    Value *codegen() override;
    raw_ostream &dump(raw_ostream &out, int ind) override {
        ExprAST::dump(out << "var", ind);
        for (const auto &NamedVar : VarNames)
            NamedVar.second->dump(indent(out, ind) << NamedVar.first << ':', ind + 1);
        Body->dump(indent(out, ind) << "Body:", ind + 1);
        return out;
    }    
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes), as well as if it is an operator.
class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;
    bool IsOperator;
    unsigned Precedence; // Precedence if a binary op.
    int Line;

public:
    PrototypeAST(SourceLocation Loc, const std::string &Name,
                 std::vector<std::string> Args, bool IsOperator = false,
                 unsigned Prec = 0)
        : Name(Name), Args(std::move(Args)), IsOperator(IsOperator),
          Precedence(Prec), Line(Loc.Line) {}
    Function *codegen();
    const std::string &getName() const { return Name; }

    bool isUnaryOp() const { return IsOperator && Args.size() == 1; }
    bool isBinaryOp() const { return IsOperator && Args.size() == 2; }

    char getOperatorName() const {
        assert(isUnaryOp() || isBinaryOp());
        return Name[Name.size() - 1];
    }

    unsigned getBinaryPrecedence() const { return Precedence; }
    int getLine() const { return Line; }
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
    Function *codegen();
    raw_ostream &dump(raw_ostream &out, int ind) {
        indent(out, ind) << "FunctionAST\n";
        ++ind;
        indent(out, ind) << "Body:";
        return Body ? Body->dump(out, ind) : out << "null\n";
    }
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
    if (!isascii(CurTok))
        return -1;

    // Make sure it's a declared binop.
    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0)
        return -1;
    return TokPrec;
}

/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken(); // consume the number
    return std::move(Result);
}

/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken(); // eat (.
    auto V = ParseExpression();
    if (!V)
        return nullptr;
    
    if (CurTok != ')')
        return LogError("expected ')'");
    getNextToken(); // eat ).
    return V;
}

/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr;

    SourceLocation LitLoc = CurLoc;

    getNextToken(); // eat identifier.

    if (CurTok != '(') // Simple variable ref.
        return std::make_unique<VariableExprAST>(LitLoc, IdName);

    // Call.
    getNextToken(); // eat (
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')') {
        while (1) {
            if (auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else
                return nullptr;

            if (CurTok == ')')
                break;

            if (CurTok != ',')
                return LogError("Expected ')' or ',' in argument list");
            getNextToken();
        }
    }

    // Eat the ')'.
    getNextToken();

    return std::make_unique<CallExprAST>(LitLoc, IdName, std::move(Args));
}

/// ifexpr ::= 'if' expression 'then' expression 'else' expression
static std::unique_ptr<ExprAST> ParseIfExpr() {
    SourceLocation IfLoc = CurLoc;

    getNextToken(); // eat the if.

    // condition.
    auto Cond = ParseExpression();
    if (!Cond)
        return nullptr;

    if (CurTok != tok_then)
        return LogError("expected then");
    getNextToken(); // eat the then

    auto Then = ParseExpression();
    if (!Then)
        return nullptr;
    
    if (CurTok != tok_else)
        return LogError("expected else");
    
    getNextToken();

    auto Else = ParseExpression();
    if (!Else)
        return nullptr;
    
    return std::make_unique<IfExprAST>(IfLoc, std::move(Cond), std::move(Then),
                                        std::move(Else));
}

/// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
static std::unique_ptr<ExprAST> ParseForExpr() {
    getNextToken(); // eat the for.

    if (CurTok != tok_identifier)
        return LogError("expected identifier after for");
    
    std::string IdName = IdentifierStr;
    getNextToken(); // eat identifier.

    if (CurTok != '=')
        return LogError("expected '=' after for");
    getNextToken(); // eat '='.

    auto Start = ParseExpression();
    if (!Start)
        return nullptr;
    if (CurTok != ',')
        return LogError("expected ',' after for start value");
    getNextToken();

    auto End = ParseExpression();
    if (!End)
        return nullptr;

    // The step value is optional.
    std::unique_ptr<ExprAST> Step;
    if (CurTok == ',') {
        getNextToken();
        Step = ParseExpression();
        if (!Step)
            return nullptr;
    }

    if (CurTok != tok_in)
        return LogError("expected 'in' after for");
    getNextToken(); // eat 'in'.

    auto Body = ParseExpression();
    if (!Body)
        return nullptr;

    return std::make_unique<ForExprAST>(IdName, std::move(Start), std::move(End),
                                        std::move(Step), std::move(Body));
}

/// varexpr ::= 'var' identifier ('=' expression)?
//                    (',' identifier ('=' expression)?)* 'in' expression
static std::unique_ptr<ExprAST> ParseVarExpr() {
    getNextToken(); // eat the var.
    
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

    if (CurTok != tok_identifier)
        return LogError("expected identifier after var");
    
    while (1) {
        std::string Name = IdentifierStr;
        getNextToken(); // eat identifier.
        
        // Read the optional initializer.
        std::unique_ptr<ExprAST> Init = nullptr;
        if (CurTok == '=') {
            getNextToken(); // eat the '='.

            Init = ParseExpression();
            if (!Init)
                return nullptr;
        }
    
        VarNames.push_back(std::make_pair(Name, std::move(Init)));

        if (CurTok != ',')
            break;
        getNextToken(); // eat the ','.

        if (CurTok != tok_identifier) 
            return LogError("expected identifier list after var");
    }
    
    if (CurTok != tok_in)
        return LogError("expected 'in' keyword after 'var'");
    getNextToken(); // eat 'in'.

    auto Body = ParseExpression();
    if (!Body)
        return nullptr;
    
    return std::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
///   ::= ifexpr
///   ::= forexpr
///   ::= varexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
        default:
            return LogError("unknown token when expecting an expression");
        case tok_identifier:
            return ParseIdentifierExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
        case tok_if:
            return ParseIfExpr();
        case tok_for:
            return ParseForExpr();
        case tok_var:
            return ParseVarExpr();
    }
}

/// unary
///   ::= primary
///   ::= '!' unary
static std::unique_ptr<ExprAST> ParseUnary() {
    if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
        return ParsePrimary();
    
    int Opc = CurTok;
    getNextToken();
    if (auto Operand = ParseUnary())
        return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
    return nullptr;
}

/// binoprhs
///   ::= ('+' unary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
    while (1) {
        int TokPrec = GetTokPrecedence();

        if (TokPrec < ExprPrec)
            return LHS;
        
        int BinOp = CurTok;
        SourceLocation BinLoc = CurLoc;
        getNextToken(); // eat binop

        auto RHS = ParseUnary();
        if (!RHS)
            return nullptr;

        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }

        LHS = std::make_unique<BinaryExprAST>(BinLoc, BinOp, std::move(LHS),
                                              std::move(RHS));
    }
}

/// expression
///   ::= unary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParseUnary();
    if (!LHS)
        return nullptr;

    return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///   ::= id '(' id* ')'
///   ::= binary LETTER number? (id, id)
///   ::= unary LETTER (id)
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    std::string FnName;

    SourceLocation FnLoc = CurLoc;

    unsigned Kind = 0; // 0 = identifier, 1 = unary, 2 = binary.
    unsigned BinaryPrecedence = 30;

    switch (CurTok) {
        default:
            return LogErrorP("Expected function name in prototype");
        case tok_identifier:
            FnName = IdentifierStr;
            Kind = 0;
            getNextToken();
            break;
        case tok_unary:
            getNextToken();
            if (!isascii(CurTok))
                return LogErrorP("Expected unary operator");
            FnName = "unary";
            FnName += (char)CurTok;
            Kind = 1;
            getNextToken();
            break;
        case tok_binary:
            getNextToken();
            if (!isascii(CurTok))
                return LogErrorP("Expected binary operator");
            FnName = "binary";
            FnName += (char)CurTok;
            Kind = 2;
            getNextToken();

            // Read the precedence if present.
            if (CurTok == tok_number) {
                if (NumVal < 1 || NumVal > 100)
                    return LogErrorP("Invalid precedence: must be 1..100");
                BinaryPrecedence = (unsigned)NumVal;
                getNextToken();
            }
            break;
    }

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype");
    
    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype");
    
    // success.
    getNextToken(); // eat ')'.
    
    if (Kind && ArgNames.size() != Kind)
        return LogErrorP("Invalid number of operands for operator");
    
    return std::make_unique<PrototypeAST>(FnLoc, FnName, ArgNames, Kind != 0,
                                          BinaryPrecedence);
}

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); // eat def.
    auto Proto = ParsePrototype();
    if (!Proto)
        return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    SourceLocation FnLoc = CurLoc;
    if (auto E = ParseExpression()) {
        auto Proto = std::make_unique<PrototypeAST>(FnLoc, "__anon_expr",
                                                 std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); // eat extern.
    return ParsePrototype();
}

//===----------------------------------------------------------------------===//
// Debug Info Support
//===----------------------------------------------------------------------===//

static std::unique_ptr<DIBuilder> DBuilder;

DIType *DebugInfo::getDoubleTy() {
    if (DblTy)
        return DblTy;

    DblTy = DBuilder->createBasicType("double", 64, dwarf::DW_ATE_float);
    return DblTy;
}

void DebugInfo::emitLocation(ExprAST *AST) {
    if (!AST)
        return Builder.SetCurrentDebugLocation(DebugLoc());
    DIScope *Scope;
    if (LexicalBlocks.empty())
        Scope = TheCU;
    else 
        Scope = LexicalBlocks.back();
    Builder.SetCurrentDebugLocation(
        DebugLoc::get(AST->getLine(), AST->getCol(), Scope));
}

static DISubroutineType *CreateFunctionType(unsigned NumArgs, DIFile *Unit) {
    SmallVector<Metadata *, 8> EltTys;
    DIType *DblTy = KSDbgInfo.getDoubleTy();

    EltTys.push_back(DblTy);

    for (unsigned i = 0, e = NumArgs; i != e; ++i)
        EltTys.push_back(DblTy);
    
    return DBuilder->createSubroutineType(DBuilder->getOrCreateTypeArray(EltTys));
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

static std::unique_ptr<Module> TheModule;
static std::map<std::string, AllocaInst *> NamedValues;
static std::unique_ptr<KaleidoscopeJIT> TheJIT;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

Function *getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // If no existing prototype exists, return null.
  return nullptr;
}

/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          StringRef VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(TheContext), nullptr, VarName);
}

Value *NumberExprAST::codegen() {
    KSDbgInfo.emitLocation(this);
    return ConstantFP::get(TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
    Value *V = NamedValues[Name];
    if (!V)
        return LogErrorV("Unknown variable name");

    KSDbgInfo.emitLocation(this);
    return Builder.CreateLoad(V, Name.c_str());
}

Value *UnaryExprAST::codegen() {
    Value *OperandV = Operand->codegen();
    if (!OperandV)
        return nullptr;
    
    Function *F = getFunction(std::string("unary") + Opcode);
    if (!F)
        return LogErrorV("Unknown unary operator");
    
    KSDbgInfo.emitLocation(this);
    return Builder.CreateCall(F, OperandV, "unop");
}

Value *BinaryExprAST::codegen() {
    KSDbgInfo.emitLocation(this);

    // Special case '=' because we don't want to emit the LHS as an expression.
    if (Op == '=') {
        // Assignment requires the LHS to be an identifier.
        // This assume we're building without RTTI because LLVM builds that way by
        // default.  If you build LLVM with RTTI this can be changed to a
        // dynamic_cast for automatic error checking.
        VariableExprAST *LHSE = static_cast<VariableExprAST *>(LHS.get());
        if (!LHSE)
            return LogErrorV("destination of '=' must be a variable");
        // Codegen the RHS.
        Value *Val = RHS->codegen();
        if (!Val)
            return nullptr;
        
        // Look up the name.
        Value *Variable = NamedValues[LHSE->getName()];
        if (!Variable)
            return LogErrorV("Unknown variable name");
        
        Builder.CreateStore(Val, Variable);
        return Val;
    }

    Value *L = LHS->codegen();
    Value *R = RHS->codegen();
    if (!L || !R)
       return nullptr;

    switch (Op) {
    case '+':
        return Builder.CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder.CreateFSub(L, R, "subtmp");
    case '*':
        return Builder.CreateFMul(L, R, "multmp");
    case '<': 
        L = Builder.CreateFCmpULT(L, R, "cmptmp");
        // Convert bool 0/1 to double 0.0 or 1.0
        return Builder.CreateUIToFP(L, Type::getDoubleTy(TheContext), "booltmp");
    default:
        break;
    }

    // If it wasn't a builtin binary operator, it must be a user defined one. Emit
    // a call to it.
    Function *F = getFunction(std::string("binary") + Op);
    assert(F && "binary operator not found!");

    Value *Ops[] = {L, R};
    return Builder.CreateCall(F, Ops, "binop");
}

Value *CallExprAST::codegen() {
    KSDbgInfo.emitLocation(this);

    Function *CalleeF = getFunction(Callee);
    if (!CalleeF)
        return LogErrorV("Unknown function referenced");

    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");

    std::vector<Value *> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }

    return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfExprAST::codegen() {
    KSDbgInfo.emitLocation(this);

    Value *CondV = Cond->codegen();
    if (!CondV)
        return nullptr;

    CondV = Builder.CreateFCmpONE(
        CondV, ConstantFP::get(TheContext, APFloat(0.0)), "ifcond");

    Function *TheFunction = Builder.GetInsertBlock()->getParent();

    BasicBlock *ThenBB = BasicBlock::Create(TheContext, "then", TheFunction);
    BasicBlock *ElseBB = BasicBlock::Create(TheContext, "else");
    BasicBlock *MergeBB = BasicBlock::Create(TheContext, "ifcont");
    
    Builder.CreateCondBr(CondV, ThenBB, ElseBB);

    Builder.SetInsertPoint(ThenBB);

    Value *ThenV = Then->codegen();
    if (!ThenV)
        return nullptr;
    
    Builder.CreateBr(MergeBB);
     // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
    ThenBB = Builder.GetInsertBlock();

    TheFunction->getBasicBlockList().push_back(ElseBB);
    Builder.SetInsertPoint(ElseBB);

    Value *ElseV = Else->codegen();
    if (!ElseV)
        return nullptr;
    
    Builder.CreateBr(MergeBB);
    ElseBB = Builder.GetInsertBlock();

    TheFunction->getBasicBlockList().push_back(MergeBB);
    Builder.SetInsertPoint(MergeBB);
    PHINode *PN = Builder.CreatePHI(Type::getDoubleTy(TheContext), 2, "iftmp");

    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    return PN;
}

// Output for-loop as:
//   var = alloca double
//   ...
//   start = startexpr
//   store start -> var
//   goto loop
// loop:
//   ...
//   bodyexpr
//   ...
// loopend:
//   step = stepexpr
//   endcond = endexpr
//
//   curvar = load var
//   nextvar = curvar + step
//   store nextvar -> var
//   br endcond, loop, endloop
// outloop:
Value *ForExprAST::codegen() {
    Function *TheFunction = Builder.GetInsertBlock()->getParent();

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);

    KSDbgInfo.emitLocation(this);

    Value *StartVal = Start->codegen();
    if (!StartVal)
        return nullptr;
    
    // Store the value into the alloca.
    Builder.CreateStore(StartVal, Alloca);

    BasicBlock *LoopBB = BasicBlock::Create(TheContext, "loop", TheFunction);

    Builder.CreateBr(LoopBB);

    Builder.SetInsertPoint(LoopBB);

    AllocaInst *OldVal = NamedValues[VarName];
    NamedValues[VarName] = Alloca;

    if (!Body->codegen())
        return nullptr;
    
    Value *StepVal = nullptr;
    if (Step) {
        StepVal = Step->codegen();
        if (!StepVal)
            return nullptr;
    } else {
        StepVal = ConstantFP::get(TheContext, APFloat(1.0));
    }

    Value *EndCond = End->codegen();
    if (!EndCond)
        return nullptr;
    
    Value *CurVar = Builder.CreateLoad(Alloca, VarName.c_str());
    Value *NextVar = Builder.CreateFAdd(CurVar, StepVal, "nextvar");
    Builder.CreateStore(NextVar, Alloca);

    EndCond = Builder.CreateFCmpONE(
        EndCond, ConstantFP::get(TheContext, APFloat(0.0)), "loopcond");
    
    BasicBlock *AfterBB = 
        BasicBlock::Create(TheContext, "afterloop", TheFunction);
    
    Builder.CreateCondBr(EndCond, LoopBB, AfterBB);

    // Any new code will be inserted in AfterBB.
    Builder.SetInsertPoint(AfterBB);

    // Restore the unshadowed variable.
    if (OldVal)
        NamedValues[VarName] = OldVal;
    else
        NamedValues.erase(VarName);
    
    return Constant::getNullValue(Type::getDoubleTy(TheContext));
}

Value *VarExprAST::codegen() {
    std::vector<AllocaInst *> OldBindings;

    Function *TheFunction = Builder.GetInsertBlock()->getParent();

    for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
        const std::string &VarName = VarNames[i].first;
        ExprAST *Init = VarNames[i].second.get();

        Value *InitVal;
        if (Init) {
            InitVal = Init->codegen();
            if (!InitVal)
                return nullptr;
        } else { // If not specified, use 0.0.
            InitVal = ConstantFP::get(TheContext, APFloat(0.0));
        }

        AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
        Builder.CreateStore(InitVal, Alloca);

        OldBindings.push_back(NamedValues[VarName]);

        NamedValues[VarName] = Alloca;
    }

    KSDbgInfo.emitLocation(this);

    Value *BodyVal = Body->codegen();
    if (!BodyVal)
        return nullptr;
    
    for (unsigned i = 0, e = VarNames.size(); i != e; ++i)
        NamedValues[VarNames[i].first] = OldBindings[i];
    
    return BodyVal;
}

Function *PrototypeAST::codegen() {
    std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(TheContext));
    FunctionType *FT =
        FunctionType::get(Type::getDoubleTy(TheContext), Doubles, false);
    
    Function *F =
        Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);
    
    return F;
}

Function *FunctionAST::codegen() {
    // Transfer ownership of the prototype to the FunctionProtos map, but keep a
    // reference to it for use below.
    auto &P = *Proto;
    FunctionProtos[Proto->getName()] = std::move(Proto);
    Function *TheFunction = getFunction(P.getName());
    if (!TheFunction)
        return nullptr;
    
    if (P.isBinaryOp())
        BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

    BasicBlock *BB = BasicBlock::Create(TheContext, "entry", TheFunction);
    Builder.SetInsertPoint(BB);

    DIFile *Unit = DBuilder->createFile(KSDbgInfo.TheCU->getFilename(),
                                        KSDbgInfo.TheCU->getDirectory());
    DIScope *FContext = Unit;
    unsigned LineNo = P.getLine();
    unsigned ScopeLine = LineNo;
    DISubprogram *SP = DBuilder->createFunction(
        FContext, P.getName(), StringRef(), Unit, LineNo,
        CreateFunctionType(TheFunction->arg_size(), Unit), ScopeLine,
        DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
    TheFunction->setSubprogram(SP);

    KSDbgInfo.LexicalBlocks.push_back(SP);

    KSDbgInfo.emitLocation(nullptr);

    NamedValues.clear();
    unsigned ArgIdx = 0;
    for (auto &Arg : TheFunction->args()) {
        AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

        DILocalVariable *D = DBuilder->createParameterVariable(
            SP, Arg.getName(), ++ArgIdx, Unit, LineNo, KSDbgInfo.getDoubleTy(),
            true);
        
        DBuilder->insertDeclare(Alloca, D, DBuilder->createExpression(),
                                DebugLoc::get(LineNo, 0, SP),
                                Builder.GetInsertBlock());

        Builder.CreateStore(&Arg, Alloca);

        NamedValues[std::string(Arg.getName())] = Alloca;
    }

    KSDbgInfo.emitLocation(Body.get());

    if (Value *RetVal = Body->codegen()) {
        Builder.CreateRet(RetVal);

        KSDbgInfo.LexicalBlocks.pop_back();

        verifyFunction(*TheFunction);

        return TheFunction;
    }

    TheFunction->eraseFromParent();
     
     if (P.isBinaryOp())
        BinopPrecedence.erase(Proto->getOperatorName());
    
    KSDbgInfo.LexicalBlocks.pop_back();

    return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static void InitializeModule() {
    // Open a new module.
    TheModule = std::make_unique<Module>("my cool jit", TheContext);
    TheModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());
}

static void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (!FnAST->codegen())
            fprintf(stderr, "Error reading function definition:");
    } else {
        getNextToken();
    }
}

static void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (!ProtoAST->codegen())
            fprintf(stderr, "Error reading extern");
        else
            FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    } else {
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto FnAST = ParseTopLevelExpr()) {
       if (!FnAST->codegen()) {
        fprintf(stderr, "Error generating code for top level expr");
       }
    } else {
        getNextToken();
    }
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
    while (1) {
        switch (CurTok) {
            case tok_eof:
                return;
            case ';': // ignore top-level semicolons.
                getNextToken();
                break;   
            case tok_def:
                HandleDefinition();
                break;   
            case tok_extern:
                HandleExtern();
                break;
            default:
                HandleTopLevelExpression();
                break;
        }
    }
}

//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    // Install standard binary operators.
    // 1 is lowest precedence.
    BinopPrecedence['='] = 2;
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40; // highest.

    // Prime the first token.
    getNextToken();

    TheJIT = std::make_unique<KaleidoscopeJIT>();

    InitializeModule();

    TheModule->addModuleFlag(Module::Warning, "Debug Info Version",
                            DEBUG_METADATA_VERSION);
    
    if (Triple(sys::getProcessTriple()).isOSDarwin())
        TheModule->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);
    
    DBuilder = std::make_unique<DIBuilder>(*TheModule);

    KSDbgInfo.TheCU = DBuilder->createCompileUnit(
        dwarf::DW_LANG_C, DBuilder->createFile("fib.ks", "."),
        "Kaleidoscope Compiler", 0, "", 0);
    
    // Run the main "interpreter loop" now.
    MainLoop();

    DBuilder->finalize();

    TheModule->print(errs(), nullptr);

    return 0;
} 