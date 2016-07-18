/* Copyright 2013-2016 Bas van den Berg
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <vector>
#include <set>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include "IRGenerator/InterfaceGenerator.h"
#include "CGenerator/HeaderNamer.h"
#include "AST/Module.h"
#include "AST/AST.h"
#include "AST/Type.h"
#include "AST/Decl.h"
#include "AST/Expr.h"
#include "FileUtils/FileUtils.h"
#include "Utils/UtilsConstants.h"

//#define CCODE_DEBUG
#ifdef CCODE_DEBUG
#include "Utils/color.h"
#include <iostream>
#define LOG_FUNC std::cerr << ANSI_BLUE << __func__ << "()" << ANSI_NORMAL << "\n";
#define LOG_DECL(_d) std::cerr << ANSI_BLUE << __func__ << "() " << ANSI_YELLOW  << _d->getName()<< ANSI_NORMAL << "\n";
#else
#define LOG_FUNC
#define LOG_DECL(_d)
#endif

using namespace C2;
using namespace clang;

InterfaceGenerator::InterfaceGenerator(const Module& module_)
    : module(module_)
    , currentAST(0)
{}

void InterfaceGenerator::write(const std::string& ifaceDir, bool printCode) {
    iface << "// WARNING: this file is auto-generated by the C2 compiler.\n";
    iface << "// Any changes you make might be lost!`\n\n";

    iface << "module " << module.getName() << ";\n";
    iface << '\n';

    Files files = module.getFiles();
    // ImportDecls
    {
        StringList importList;
        importList.push_back(module.getName());       // add self to avoid importing self
        for (unsigned a=0; a<files.size(); a++) {
            currentAST = files[a];
            for (unsigned i=0; i<currentAST->numImports(); i++) {
                const ImportDecl* I = currentAST->getImport(i);
                if (!I->isUsedPublic()) continue;
                EmitImport(I, importList);
            }
        }
        iface << '\n';
    }

    // TypeDecls
    for (unsigned a=0; a<files.size(); a++) {
        currentAST = files[a];
        for (unsigned i=0; i<currentAST->numTypes(); i++) {
            const TypeDecl* T = currentAST->getType(i);
            if (!T->isPublic()) continue;
            EmitTypeDecl(T);
            iface << '\n';
        }
    }

    // VarDecls
    for (unsigned a=0; a<files.size(); a++) {
        currentAST = files[a];
        for (unsigned i=0; i<currentAST->numVars(); i++) {
            const VarDecl* V = currentAST->getVar(i);
            if (!V->isPublic()) continue;
            EmitVarDecl(V, 0);
            iface << ";\n\n";
        }
    }

    // FunctionDecls
    for (unsigned a=0; a<files.size(); a++) {
        currentAST = files[a];
        for (unsigned i=0; i<currentAST->numFunctions(); i++) {
            const FunctionDecl* F = currentAST->getFunction(i);
            if (!F->isPublic()) continue;
            EmitFunctionDecl(F);
            iface << "\n";
        }
    }

    // ArrayValueDecls
    //unsigned numArrayValues() const { return arrayValues.size(); }
    //ArrayValueDecl* getArrayValue(unsigned i) const { return arrayValues[i]; }

    // write file
    // TODO handle errors
    std::string filename = module.getName() + ".c2i";
    FileUtils::writeFile(ifaceDir.c_str(), ifaceDir + filename, iface);

    if (printCode) {
        printf("---- code for %s ----\n%s\n", filename.c_str(), (const char*)iface);
    }
}

void InterfaceGenerator::EmitExpr(const Expr* E) {
    LOG_FUNC
    switch (E->getKind()) {
    case EXPR_INTEGER_LITERAL:
        {
            const IntegerLiteral* N = cast<IntegerLiteral>(E);
            iface.number(N->getRadix(), N->Value.getSExtValue());
            return;
        }
    case EXPR_FLOAT_LITERAL:
        {
            const FloatingLiteral* F = cast<FloatingLiteral>(E);
            char temp[20];
            sprintf(temp, "%f", F->Value.convertToFloat());
            iface << temp;
            return;
        }
    case EXPR_BOOL_LITERAL:
        {
            const BooleanLiteral* B = cast<BooleanLiteral>(E);
            iface << (int)B->getValue();
            return;
        }
    case EXPR_CHAR_LITERAL:
        {
            const CharacterLiteral* C = cast<CharacterLiteral>(E);
            C->printLiteral(iface);
            return;
        }
    case EXPR_STRING_LITERAL:
        {
            const StringLiteral* S = cast<StringLiteral>(E);
            EmitStringLiteral(S->value);
            return;
        }
    case EXPR_NIL:
        iface << "NULL";
        return;
    case EXPR_CALL:
        assert(0 && "cannot happen");
        return;
    case EXPR_IDENTIFIER:
        EmitIdentifierExpr(cast<IdentifierExpr>(E));
        return;
    case EXPR_INITLIST:
        {
            const InitListExpr* I = cast<InitListExpr>(E);
            iface << "{ ";
            const ExprList& values = I->getValues();
            for (unsigned i=0; i<values.size(); i++) {
                if (i == 0 && values[0]->getKind() == EXPR_INITLIST) iface << '\n';
                EmitExpr(values[i]);
                if (i != values.size() -1) iface << ", ";
                if (values[i]->getKind() == EXPR_INITLIST) iface << '\n';
            }
            iface << " }";
            return;
        }
    case EXPR_DESIGNATOR_INIT:
        {
            const DesignatedInitExpr* D = cast<DesignatedInitExpr>(E);
            if (D->getDesignatorKind() == DesignatedInitExpr::ARRAY_DESIGNATOR) {
                iface << '[';
                EmitExpr(D->getDesignator());
                iface << "] = ";
            } else {
                iface << '.' << D->getField() << " = ";
            }
            EmitExpr(D->getInitValue());
            return;
        }
    case EXPR_TYPE:
        {
            //const TypeExpr* T = cast<TypeExpr>(E);
            //EmitTypePreName(T->getType(), iface);
            //EmitTypePostName(T->getType(), iface);
            return;
        }
    case EXPR_BINOP:
        EmitBinaryOperator(E);
        return;
    case EXPR_CONDOP:
        EmitConditionalOperator(E);
        return;
    case EXPR_UNARYOP:
        EmitUnaryOperator(E);
        return;
    case EXPR_BUILTIN:
        {
            const BuiltinExpr* S = cast<BuiltinExpr>(E);
            if (S->isSizeof()) {
                iface << "sizeof(";
                EmitExpr(S->getExpr());
                iface << ')';
            } else {
                const IdentifierExpr* I = cast<IdentifierExpr>(S->getExpr());
                Decl* D = I->getDecl();
                // should be VarDecl(for array/enum) or TypeDecl(array/enum)
                switch (D->getKind()) {
                case DECL_FUNC:
                    assert(0);
                    break;
                case DECL_VAR:
                    {
                        VarDecl* VD = cast<VarDecl>(D);
                        QualType Q = VD->getType();
                        if (Q.isArrayType()) {
                            // generate: (sizeof(array) / sizeof(array[0]))
                            iface << "(sizeof(";
                            //EmitDecl(D, iface);
                            iface << ")/sizeof(";
                            //EmitDecl(D, iface);
                            iface << "[0]))";
                            return;
                        }
                        // TODO also allow elemsof for EnumType
                        // NOTE cannot be converted to C if used with enums
                        assert(0 && "TODO");
                        return;
                    }
                case DECL_ENUMVALUE:
                    break;
                case DECL_ALIASTYPE:
                case DECL_STRUCTTYPE:
                case DECL_ENUMTYPE:
                case DECL_FUNCTIONTYPE:
                case DECL_ARRAYVALUE:
                case DECL_IMPORT:
                case DECL_LABEL:
                    assert(0);
                    break;
                }
            }
            return;
        }
    case EXPR_ARRAYSUBSCRIPT:
        {
            const ArraySubscriptExpr* A = cast<ArraySubscriptExpr>(E);
            if (isa<BitOffsetExpr>(A->getIndex())) {
                assert(0 && "todo?");
            } else {
                EmitExpr(A->getBase());
                iface << '[';
                EmitExpr(A->getIndex());
                iface << ']';
            }
            return;
        }
    case EXPR_MEMBER:
        EmitMemberExpr(E);
        return;
    case EXPR_PAREN:
        {
            const ParenExpr* P = cast<ParenExpr>(E);
            iface << '(';
            EmitExpr(P->getExpr());
            iface << ')';
            return;
        }
    case EXPR_BITOFFSET:
        assert(0 && "should not happen");
        break;
    case EXPR_CAST:
        {
            const ExplicitCastExpr* ECE = cast<ExplicitCastExpr>(E);
            iface << '(';
            //EmitTypePreName(ECE->getDestType());
            //EmitTypePostName(ECE->getDestType());
            iface << ")(";
            EmitExpr(ECE->getInner());
            iface << ')';
            return;
        }
    }
}

void InterfaceGenerator::EmitBinaryOperator(const Expr* E) {
    LOG_FUNC
    const BinaryOperator* B = cast<BinaryOperator>(E);
    EmitExpr(B->getLHS());
    iface << ' ' << BinaryOperator::OpCode2str(B->getOpcode()) << ' ';
    EmitExpr(B->getRHS());
}

void InterfaceGenerator::EmitConditionalOperator(const Expr* E) {
    LOG_FUNC
    const ConditionalOperator* C = cast<ConditionalOperator>(E);
    EmitExpr(C->getCond());
    iface << " ? ";
    EmitExpr(C->getLHS());
    iface << " : ";
    EmitExpr(C->getRHS());

}

void InterfaceGenerator::EmitUnaryOperator(const Expr* E) {
    LOG_FUNC
    const UnaryOperator* U = cast<UnaryOperator>(E);

    switch (U->getOpcode()) {
    case UO_PostInc:
    case UO_PostDec:
        EmitExpr(U->getExpr());
        iface << UnaryOperator::OpCode2str(U->getOpcode());
        break;
    case UO_PreInc:
    case UO_PreDec:
    case UO_AddrOf:
    case UO_Deref:
    case UO_Plus:
    case UO_Minus:
    case UO_Not:
    case UO_LNot:
        //iface.indent(indent);
        iface << UnaryOperator::OpCode2str(U->getOpcode());
        EmitExpr(U->getExpr());
        break;
    default:
        assert(0);
    }
}

void InterfaceGenerator::EmitMemberExpr(const Expr* E) {
    LOG_FUNC
    const MemberExpr* M = cast<MemberExpr>(E);
    if (M->isModulePrefix()) {
        EmitIdentifierExpr(M->getMember());
    } else {
        assert(0 && "can this happen?");
    }
}

void InterfaceGenerator::EmitIdentifierExpr(const IdentifierExpr* E) {
    LOG_FUNC
    const Decl* D = E->getDecl();
    EmitPrefixedDecl(D);
}

void InterfaceGenerator::EmitImport(const ImportDecl* D, StringList& importList) {
    LOG_DECL(D)
    const std::string& name = D->getModuleName();

    for (StringListConstIter iter = importList.begin(); iter != importList.end(); ++iter) {
        if (*iter == name) return;
    }

    iface << "import " << name << ";\n";
    importList.push_back(name);
}

void InterfaceGenerator::EmitFunctionArgs(const FunctionDecl* F) {
    LOG_DECL(F)
    iface << '(';
    int count = F->numArgs();
    if (F->isVariadic()) count++;
    for (unsigned i=0; i<F->numArgs(); i++) {
        VarDecl* A = F->getArg(i);
        EmitArgVarDecl(A, i);
        if (count != 1) iface << ", ";
        count--;
    }
    if (F->isVariadic()) iface << "...";
    iface << ')';
}

void InterfaceGenerator::EmitTypeDecl(const TypeDecl* T) {
    LOG_DECL(T)

    switch (T->getKind()) {
    case DECL_FUNC:
    case DECL_VAR:
    case DECL_ENUMVALUE:
        assert(0);
        break;
    case DECL_ALIASTYPE:
        EmitAliasType(T);
        break;
    case DECL_STRUCTTYPE:
        EmitStructType(cast<StructTypeDecl>(T), 0);
        return;
    case DECL_ENUMTYPE:
        EmitEnumType(cast<EnumTypeDecl>(T));
        return;
    case DECL_FUNCTIONTYPE:
        EmitFunctionType(cast<FunctionTypeDecl>(T));
        return;
    case DECL_ARRAYVALUE:
    case DECL_IMPORT:
    case DECL_LABEL:
        assert(0);
        break;
    }
}

void InterfaceGenerator::EmitAliasType(const TypeDecl* T) {
    LOG_DECL(T);
    iface << "type " << T->getName() << ' ';
    const AliasTypeDecl* ATD = cast<AliasTypeDecl>(T);
    EmitType(ATD->getRefType());
    iface << ";\n";
}

void InterfaceGenerator::EmitStructType(const StructTypeDecl* S, unsigned indent) {
    LOG_DECL(S)
    if (S->isGlobal()) {
        iface << "type " << S->getName() << ' ';
        iface << (S->isStruct() ? "struct" : "union");
    } else {
        iface.indent(indent);
        iface << (S->isStruct() ? "struct" : "union");
        if (S->getName() != "") {
            iface << ' ' << S->getName();
        }
    }

    if (S->hasAttribute(ATTR_OPAQUE)) {
        iface << " {}";
        EmitAttributes(S);
        iface << '\n';
        return;
    }

    iface << " {\n";
    for (unsigned i=0;i<S->numMembers(); i++) {
        Decl* member = S->getMember(i);
        if (isa<VarDecl>(member)) {
            EmitVarDecl(cast<VarDecl>(member), indent + INDENT);
            iface << ";\n";
        } else if (isa<StructTypeDecl>(member)) {
            EmitStructType(cast<StructTypeDecl>(member), indent+INDENT);
        } else {
            assert(0);
        }
    }
    iface.indent(indent);
    iface << '}';
    if (S->getName() != "" && !S->isGlobal()) {
        iface << ' ';
        //EmitDecl(S);
    }
    EmitAttributes(S);
    iface << '\n';
}

void InterfaceGenerator::EmitEnumType(const EnumTypeDecl* E) {
    LOG_DECL(E)
    iface << "type " << E->getName() << " enum ";
    EmitType(E->getImplType());
    iface << " {\n";
    for (unsigned i=0; i<E->numConstants(); i++) {
        EnumConstantDecl* C = E->getConstant(i);
        iface.indent(INDENT);
        iface << C->getName();
        if (C->getInitValue()) {
            iface << " = ";
            EmitExpr(C->getInitValue());
        }
        iface << ",\n";
    }
    iface << "}";
    EmitAttributes(E);
    iface << '\n';
}

void InterfaceGenerator::EmitFunctionType(const FunctionTypeDecl* FTD) {
    LOG_DECL(FTD)
    iface << "type " << FTD->getName() << " func ";
    FunctionDecl* F = FTD->getDecl();
    EmitType(F->getReturnType());
    EmitFunctionArgs(F);
    EmitAttributes(FTD);
    iface << ";\n";
}

void InterfaceGenerator::EmitPrefixedDecl(const Decl* D) {
    assert(D);
    // TODO compare by pointer instead of string
    const std::string& mname = D->getModule()->getName();
    // add prefix here, since we might have changed the ImportDecl (removed alias/local)
    if (mname != module.getName()) {
        iface << mname << '.';
    }
    iface << D->getName();
}

void InterfaceGenerator::EmitType(QualType type) {
    LOG_FUNC
    if (type.isConstQualified()) iface << "const ";

    const Type* T = type.getTypePtr();
    switch (T->getTypeClass()) {
    case TC_BUILTIN:
        {
            // TODO handle Qualifiers
            const BuiltinType* BI = cast<BuiltinType>(T);
            iface << BuiltinType::kind2name(BI->getKind());
            break;
        }
    case TC_POINTER:
        // TODO handle Qualifiers
        EmitType(cast<PointerType>(T)->getPointeeType());
        iface << '*';
        break;
    case TC_ARRAY:
        {
            // TODO handle Qualifiers
            EmitType(cast<ArrayType>(T)->getElementType());

            // TEMP, use canonical type, since type can be AliasType
            type = type.getCanonicalType();
            const ArrayType* A = cast<ArrayType>(type);
            iface << '[';
            if (A->getSizeExpr()) {
                EmitExpr(A->getSizeExpr());
            }
            iface << ']';
            break;
        }
    case TC_UNRESOLVED:
        assert(0 && "should be resolved");
        break;
    case TC_ALIAS:
        EmitPrefixedDecl(cast<AliasType>(T)->getDecl());
        break;
    case TC_STRUCT:
        EmitPrefixedDecl(cast<StructType>(T)->getDecl());
        break;
    case TC_ENUM:
        EmitPrefixedDecl(cast<EnumType>(T)->getDecl());
        break;
    case TC_FUNCTION:
        EmitPrefixedDecl(cast<FunctionType>(T)->getDecl());
        break;
    case TC_MODULE:
        assert(0 && "should not happen");
        break;
    }
}

void InterfaceGenerator::EmitArgVarDecl(const VarDecl* D, unsigned index) {
    LOG_DECL(D)
    EmitType(D->getType());
    iface << ' '<< D->getName();

    const Expr* init = D->getInitValue();
    if (init) {
        iface << " = ";
        EmitExpr(init);
    }
}

void InterfaceGenerator::EmitFunctionDecl(const FunctionDecl* F) {
    LOG_DECL(F)
    // TODO
    iface << "func ";
    EmitType(F->getReturnType());
    iface << ' ' << F->getName();
    EmitFunctionArgs(F);
    EmitAttributes(F);
    iface << ";\n";
}

void InterfaceGenerator::EmitVarDecl(const VarDecl* D, unsigned indent) {
    LOG_DECL(D)
    iface.indent(indent);
    EmitType(D->getType());
    iface << ' ' << D->getName();

    QualType T = D->getType();
    if (T.isConstQualified()) {
        assert(D->getInitValue());
        iface << " = ";
        EmitExpr(D->getInitValue());
    }
}

void InterfaceGenerator::EmitStringLiteral(const std::string& input) {
    LOG_FUNC
    // always cast to 'unsigned char*'
    iface << '"';
    const char* cp = input.c_str();
    for (unsigned i=0; i<input.size(); i++) {
        switch (*cp) {
        case '\n':
            iface << "\\n";
            break;
        case '\r':
            iface << "\\r";
            break;
        case '\t':
            iface << "\\t";
            break;
        case '\033':
            iface << "\\033";
            break;
        // TODO other escaped chars
        default:
            iface << *cp;
            break;
        }
        cp++;
    }
    iface << '"';
}

void InterfaceGenerator::EmitAttributes(const Decl* D) {
    if (!D->hasAttributes()) return;

    bool first = true;
    const AttrList& AL = D->getAttributes();
    for (AttrListConstIter iter = AL.begin(); iter != AL.end(); ++iter) {
        const Attr* A = *iter;
        const Expr* Arg = A->getArg();
        switch (A->getKind()) {
        case ATTR_UNKNOWN:
        case ATTR_EXPORT:
        case ATTR_NORETURN:
        case ATTR_INLINE:
        case ATTR_UNUSED_PARAMS:
            // ignore for now
            break;
        case ATTR_WEAK:
        case ATTR_PACKED:
        case ATTR_UNUSED:
        case ATTR_SECTION:
        case ATTR_ALIGNED:
        case ATTR_OPAQUE:
            if (first) iface << " @(";
            else iface << ", ";
            iface << A->kind2str();
            if (Arg) {
                iface << '(';
                Arg->printLiteral(iface);
                iface << ')';
            }
            first = false;
            break;
        }
    }
    if (!first) iface << ")";
}

