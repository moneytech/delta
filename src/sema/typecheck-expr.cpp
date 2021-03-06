#include "typecheck.h"
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
#pragma warning(push, 0)
#include <llvm/ADT/APSInt.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/ErrorHandling.h>
#pragma warning(pop)
#include "../ast/decl.h"
#include "../ast/expr.h"
#include "../ast/module.h"
#include "../ast/type.h"

using namespace delta;

void Typechecker::checkHasAccess(const Decl& decl, SourceLocation location, AccessLevel userAccessLevel) {
    // FIXME: Compare SourceFile objects instead of file path strings.
    if (decl.getAccessLevel() == AccessLevel::Private && strcmp(decl.getLocation().file, location.file) != 0) {
        WARN(location, "'" << decl.getName() << "' is private");
    } else if (userAccessLevel != AccessLevel::None && decl.getAccessLevel() < userAccessLevel) {
        WARN(location, "using " << decl.getAccessLevel() << " type '" << decl.getName() << "' in " << userAccessLevel << " declaration");
    }
}

void Typechecker::checkLambdaCapture(const VariableDecl& variableDecl, const VarExpr& varExpr) const {
    auto* parent = getCurrentModule()->getSymbolTable().getCurrentScope().parent;
    if (parent && parent->isLambda() && variableDecl.getParent() != parent) {
        ERROR(varExpr.getLocation(), "lambda capturing not implemented yet");
    }
}

Type Typechecker::typecheckVarExpr(VarExpr& expr, bool useIsWriteOnly) {
    auto* decl = findDecl(expr.getIdentifier(), expr.getLocation());
    checkHasAccess(*decl, expr.getLocation(), AccessLevel::None);
    decl->setReferenced(true);
    expr.setDecl(decl);

    if (auto variableDecl = llvm::dyn_cast<VariableDecl>(decl)) {
        checkLambdaCapture(*variableDecl, expr);
    }

    switch (decl->getKind()) {
        case DeclKind::VarDecl:
            if (!useIsWriteOnly) checkNotMoved(*decl, expr);
            return llvm::cast<VarDecl>(decl)->getType();
        case DeclKind::ParamDecl:
            if (!useIsWriteOnly) checkNotMoved(*decl, expr);
            return llvm::cast<ParamDecl>(decl)->getType();
        case DeclKind::FunctionDecl:
        case DeclKind::MethodDecl:
            return Type(llvm::cast<FunctionDecl>(decl)->getFunctionType(), Mutability::Mutable, SourceLocation());
        case DeclKind::GenericParamDecl:
            llvm_unreachable("cannot refer to generic parameters yet");
        case DeclKind::ConstructorDecl:
            llvm_unreachable("cannot refer to constructors yet");
        case DeclKind::DestructorDecl:
            llvm_unreachable("cannot refer to destructors yet");
        case DeclKind::FunctionTemplate:
            llvm_unreachable("cannot refer to generic functions yet");
        case DeclKind::TypeDecl:
            return llvm::cast<TypeDecl>(decl)->getType();
        case DeclKind::TypeTemplate:
            llvm_unreachable("cannot refer to generic types yet");
        case DeclKind::EnumDecl:
            ERROR(expr.getLocation(), "'" << expr.getIdentifier() << "' is not a variable");
        case DeclKind::EnumCase:
            return llvm::cast<EnumCase>(decl)->getType();
        case DeclKind::FieldDecl:
            return llvm::cast<FieldDecl>(decl)->getType();
        case DeclKind::ImportDecl:
            llvm_unreachable("import statement validation not implemented yet");
    }
    llvm_unreachable("all cases handled");
}

Type typecheckStringLiteralExpr(StringLiteralExpr&) {
    return BasicType::get("string", {});
}

Type typecheckCharacterLiteralExpr(CharacterLiteralExpr&) {
    return Type::getChar();
}

Type typecheckIntLiteralExpr(IntLiteralExpr& expr) {
    if (expr.getValue().isSignedIntN(32)) {
        return Type::getInt();
    }
    if (expr.getValue().isSignedIntN(64)) {
        return Type::getInt64();
    }
    ERROR(expr.getLocation(), "integer literal is too large");
}

Type typecheckFloatLiteralExpr(FloatLiteralExpr&) {
    return Type::getFloat();
}

Type typecheckBoolLiteralExpr(BoolLiteralExpr&) {
    return Type::getBool();
}

Type typecheckNullLiteralExpr(NullLiteralExpr&) {
    return Type::getNull();
}

Type typecheckUndefinedLiteralExpr(UndefinedLiteralExpr&) {
    return Type::getUndefined();
}

Type Typechecker::typecheckArrayLiteralExpr(ArrayLiteralExpr& array, Type expectedType) {
    if (array.getElements().empty()) {
        if (expectedType) {
            return expectedType;
        } else {
            ERROR(array.getLocation(), "couldn't infer type of empty array literal");
        }
    }

    Type firstType = typecheckExpr(*array.getElements()[0]);

    for (auto& element : array.getElements().drop_front()) {
        Type type = typecheckExpr(*element);
        if (type != firstType) {
            ERROR(element->getLocation(), "mixed element types in array literal (expected '" << firstType << "', found '" << type << "')");
        }
    }

    return ArrayType::get(firstType, int64_t(array.getElements().size()));
}

Type Typechecker::typecheckTupleExpr(TupleExpr& expr) {
    auto elements = map(expr.getElements(), [&](NamedValue& namedValue) {
        return TupleElement{ namedValue.getName(), typecheckExpr(*namedValue.getValue()) };
    });
    return TupleType::get(std::move(elements));
}

Type Typechecker::typecheckUnaryExpr(UnaryExpr& expr) {
    Type operandType = typecheckExpr(expr.getOperand());

    switch (expr.getOperator()) {
        case Token::Not:
            operandType = operandType.removePointer();

            if (!operandType.isBool() && !operandType.isOptionalType()) {
                ERROR(expr.getOperand().getLocation(), "invalid operand type '" << operandType << "' to logical not");
            }

            return Type::getBool();

        case Token::Star: // Dereference operation
            if (operandType.isOptionalType() && operandType.getWrappedType().isPointerType()) {
                WARN(expr.getLocation(), "dereferencing value of optional type '"
                                             << operandType << "' which may be null; "
                                             << "unwrap the value with a postfix '!' to silence this warning");
                operandType = operandType.getWrappedType();
            }

            if (operandType.isPointerType()) {
                return operandType.getPointee();
            } else if (operandType.isArrayWithUnknownSize()) {
                return operandType.getElementType();
            }

            ERROR(expr.getLocation(), "cannot dereference non-pointer type '" << operandType << "'");

        case Token::And: // Address-of operation
            return PointerType::get(operandType);

        case Token::Increment:
            operandType = operandType.removePointer();

            if (!operandType.isMutable()) {
                ERROR(expr.getLocation(), "cannot increment immutable value of type '" << operandType << "'");
            } else if (!operandType.isIncrementable()) {
                ERROR(expr.getLocation(), "cannot increment '" << operandType << "'");
            }

            return Type::getVoid();

        case Token::Decrement:
            operandType = operandType.removePointer();

            if (!operandType.isMutable()) {
                ERROR(expr.getLocation(), "cannot decrement immutable value of type '" << operandType << "'");
            } else if (!operandType.isDecrementable()) {
                ERROR(expr.getLocation(), "cannot decrement '" << operandType << "'");
            }

            return Type::getVoid();

        default:
            return operandType;
    }
}

static void invalidOperandsToBinaryExpr(const BinaryExpr& expr, Token::Kind op) {
    std::string hint;

    if ((expr.getRHS().isNullLiteralExpr() || expr.getLHS().isNullLiteralExpr()) && (op == Token::Equal || op == Token::NotEqual)) {
        hint += " (non-optional type '";
        if (expr.getRHS().isNullLiteralExpr()) {
            hint += expr.getLHS().getType().toString(true);
        } else {
            hint += expr.getRHS().getType().toString(true);
        }
        hint += "' cannot be null)";
    } else {
        hint = "";
    }

    ERROR(expr.getLocation(), "invalid operands '" << expr.getLHS().getType() << "' and '" << expr.getRHS().getType() << "' to '"
                                                   << toString(op) << "'" << hint);
}

static bool allowAssignmentOfUndefined(const Expr& lhs, const FunctionDecl* currentFunction) {
    if (auto* constructorDecl = llvm::dyn_cast<ConstructorDecl>(currentFunction)) {
        switch (lhs.getKind()) {
            case ExprKind::VarExpr: {
                auto* fieldDecl = llvm::dyn_cast<FieldDecl>(llvm::cast<VarExpr>(lhs).getDecl());
                return fieldDecl && fieldDecl->getParent() == constructorDecl->getTypeDecl();
            }
            case ExprKind::MemberExpr: {
                auto* varExpr = llvm::dyn_cast<VarExpr>(llvm::cast<MemberExpr>(lhs).getBaseExpr());
                return varExpr && varExpr->getIdentifier() == "this";
            }
            default:
                return false;
        }
    }
    return false;
}

Type Typechecker::typecheckBinaryExpr(BinaryExpr& expr) {
    auto op = expr.getOperator();

    if (op == Token::Assignment) {
        typecheckAssignment(expr.getLHS(), expr.getRHS(), expr.getLocation());
        return Type::getVoid();
    }

    if (isCompoundAssignmentOperator(op)) {
        auto rhs = new BinaryExpr(withoutCompoundEqSuffix(op), &expr.getLHS(), &expr.getRHS(), expr.getLocation());
        expr = BinaryExpr(Token::Assignment, &expr.getLHS(), rhs, expr.getLocation());
        return typecheckBinaryExpr(expr);
    }

    Type leftType = typecheckExpr(expr.getLHS());
    Type rightType = typecheckExpr(expr.getRHS());

    if (!isBuiltinOp(op, leftType, rightType)) {
        return typecheckCallExpr(expr);
    }

    if (op == Token::AndAnd || op == Token::OrOr) {
        if (leftType.isBool() && rightType.isBool()) {
            return Type::getBool();
        }
        invalidOperandsToBinaryExpr(expr, op);
    }

    if (op == Token::PointerEqual || op == Token::PointerNotEqual) {
        if (!(leftType.removeOptional().isPointerType() || leftType.removeOptional().isArrayWithUnknownSize()) ||
            !(rightType.removeOptional().isPointerType() || rightType.removeOptional().isArrayWithUnknownSize())) {
            ERROR(expr.getLocation(), "both operands to pointer comparison operator must have pointer type");
        }

        auto leftPointeeType = leftType.removeOptional().removePointer().removeArrayWithUnknownSize();
        auto rightPointeeType = rightType.removeOptional().removePointer().removeArrayWithUnknownSize();

        if (!leftPointeeType.equalsIgnoreTopLevelMutable(rightPointeeType)) {
            WARN(expr.getLocation(), "pointers to different types are not allowed to be equal (got '" << leftType << "' and '" << rightType << "')");
        }
    }

    if (isBitwiseOperator(op) && (leftType.isFloatingPoint() || rightType.isFloatingPoint())) {
        invalidOperandsToBinaryExpr(expr, op);
    }

    bool converted = convert(&expr.getRHS(), leftType, true) || convert(&expr.getLHS(), rightType, true);

    if (!converted && (!leftType.removeOptional().isPointerType() || !rightType.removeOptional().isPointerType())) {
        invalidOperandsToBinaryExpr(expr, op);
    }

    return isComparisonOperator(op) ? Type::getBool() : expr.getLHS().getType();
}

void Typechecker::typecheckAssignment(Expr& lhs, Expr& rhs, SourceLocation location) {
    if (!lhs.isLvalue()) {
        ERROR(lhs.getLocation(), "expression is not assignable");
    }

    typecheckExpr(lhs, true);
    Type lhsType = lhs.getAssignableType();
    Type rhsType = typecheckExpr(rhs, false, lhsType);

    if (rhs.isUndefinedLiteralExpr() && !allowAssignmentOfUndefined(lhs, currentFunction)) {
        ERROR(rhs.getLocation(), "'undefined' is only allowed as an initial value");
    }

    if (!convert(&rhs, lhsType)) {
        ERROR(location, "cannot assign '" << rhsType << "' to '" << lhsType << "'");
    }

    if (!lhsType.isMutable()) {
        switch (lhs.getKind()) {
            case ExprKind::VarExpr: {
                auto identifier = llvm::cast<VarExpr>(lhs).getIdentifier();
                ERROR(location, "cannot assign to immutable variable '" << identifier << "' of type '" << lhsType << "'");
            }
            case ExprKind::MemberExpr: {
                auto memberName = llvm::cast<MemberExpr>(lhs).getMemberName();
                ERROR(location, "cannot assign to immutable variable '" << memberName << "' of type '" << lhsType << "'");
            }
            default:
                ERROR(location, "cannot assign to immutable expression of type '" << lhsType << "'");
        }
    }

    if (!rhsType.isImplicitlyCopyable() && !lhsType.removeOptional().isPointerType()) {
        setMoved(&rhs, true);
        setMoved(&lhs, false);
    }

    if (currentInitializedFields) {
        if (auto fieldDecl = lhs.getFieldDecl()) {
            currentInitializedFields->insert(fieldDecl);
        }
    }
}

static void checkRange(const Expr& expr, const llvm::APSInt& value, Type type) {
    if (llvm::APSInt::compareValues(value, llvm::APSInt::getMinValue(type.getIntegerBitWidth(), type.isUnsigned())) < 0 ||
        llvm::APSInt::compareValues(value, llvm::APSInt::getMaxValue(type.getIntegerBitWidth(), type.isUnsigned())) > 0) {
        ERROR(expr.getLocation(), value << " is out of range for type '" << type << "'");
    }
}

static bool hasField(TypeDecl& type, const FieldDecl& field) {
    return llvm::any_of(type.getFields(), [&](const FieldDecl& ownField) {
        return ownField.getName() == field.getName() && ownField.getType() == field.getType();
    });
}

bool Typechecker::hasMethod(TypeDecl& type, FunctionDecl& functionDecl) const {
    auto decls = findDecls(getQualifiedFunctionName(type.getType(), functionDecl.getName(), {}));

    for (Decl* decl : decls) {
        if (!decl->isFunctionDecl()) continue;
        if (!llvm::cast<FunctionDecl>(decl)->getTypeDecl()) continue;
        if (llvm::cast<FunctionDecl>(decl)->getTypeDecl()->getName() != type.getName()) continue;
        if (!llvm::cast<FunctionDecl>(decl)->signatureMatches(functionDecl, /* matchReceiver: */ false)) continue;
        return true;
    }

    return false;
}

bool Typechecker::providesInterfaceRequirements(TypeDecl& type, TypeDecl& interface, std::string* errorReason) const {
    auto thisTypeResolvedInterface = llvm::cast<TypeDecl>(interface.instantiate({ { "This", type.getType() } }, {}));

    for (auto& fieldRequirement : thisTypeResolvedInterface->getFields()) {
        if (!hasField(type, fieldRequirement)) {
            if (errorReason) {
                *errorReason = ("doesn't have field '" + fieldRequirement.getName() + "'").str();
            }
            return false;
        }
    }

    for (auto& requiredMethod : thisTypeResolvedInterface->getMethods()) {
        if (auto* functionDecl = llvm::dyn_cast<FunctionDecl>(requiredMethod)) {
            if (functionDecl->hasBody()) continue;

            if (!hasMethod(type, *functionDecl)) {
                if (errorReason) {
                    *errorReason = ("doesn't have member function '" + functionDecl->getName() + "'").str();
                }
                return false;
            }
        } else {
            ERROR(requiredMethod->getLocation(), "non-function interface member requirements are not supported yet");
        }
    }

    return true;
}

bool Typechecker::convert(Expr* expr, Type type, bool allowPointerToTemporary) const {
    // NOTE: convertedType may be the same as the source type, i.e. expr->getType().
    Type convertedType = isImplicitlyConvertible(expr, expr->getType(), type, allowPointerToTemporary);

    if (convertedType) {
        expr->setType(convertedType);

        if (auto* ifExpr = llvm::dyn_cast<IfExpr>(expr)) {
            ifExpr->getThenExpr()->setType(convertedType);
            ifExpr->getElseExpr()->setType(convertedType);
        }

        return true;
    } else {
        return false;
    }
}

Type Typechecker::isImplicitlyConvertible(const Expr* expr, Type source, Type target, bool allowPointerToTemporary) const {
    if (source.isBasicType() && target.isBasicType() && source.getName() == target.getName() && source.getGenericArgs() == target.getGenericArgs()) {
        return source;
    }

    if (source.isArrayType() && target.isArrayType() && source.getElementType() == target.getElementType()) {
        if (source.getArraySize() == target.getArraySize()) return source;
        if (source.isArrayWithConstantSize() && (target.isArrayWithUnknownSize() || target.isArrayWithRuntimeSize())) return source;
    }

    if (source.isTupleType() && target.isTupleType() && source.getTupleElements() == target.getTupleElements()) {
        return source;
    }

    if (source.isFunctionType() && target.isFunctionType() && source.getReturnType() == target.getReturnType() &&
        source.getParamTypes() == target.getParamTypes()) {
        return source;
    }

    if (source.isPointerType() && target.isPointerType() && (source.getPointee().isMutable() || !target.getPointee().isMutable()) &&
        (isImplicitlyConvertible(nullptr, source.getPointee(), target.getPointee()) || target.getPointee().isVoid())) {
        return source;
    }

    if (source.isOptionalType() && target.isOptionalType() && (source.getWrappedType().isMutable() || !target.getWrappedType().isMutable()) &&
        isImplicitlyConvertible(nullptr, source.getWrappedType(), target.getWrappedType())) {
        return source;
    }

    if (expr) {
        if (expr->getType().isEnumType() && llvm::cast<EnumDecl>(expr->getType().getDecl())->getTagType() == target) {
            return source;
        }

        if (auto* ifExpr = llvm::dyn_cast<IfExpr>(expr)) {
            if (isImplicitlyConvertible(ifExpr->getThenExpr(), ifExpr->getThenExpr()->getType(), target) &&
                isImplicitlyConvertible(ifExpr->getElseExpr(), ifExpr->getElseExpr()->getType(), target)) {
                return target;
            }
        }

        // Auto-cast integer constants to parameter type if within range, error out if not within range.
        if ((expr->getType().isInteger() || expr->getType().isChar() || expr->getType().isEnumType()) && expr->isConstant()) {
            const auto& value = expr->getConstantIntegerValue();

            if (target.isInteger()) {
                checkRange(*expr, value, target);
                return target;
            }

            if (target.isFloatingPoint()) {
                // TODO: Check that the integer value is losslessly convertible to the target type?
                return target;
            }
        }

        if (expr->getType().isFloatingPoint() && expr->isConstant() && target.isFloatingPoint()) {
            // TODO: Check that the floating-point value is losslessly convertible to the target type?
            return target;
        }

        if (expr->isNullLiteralExpr() && target.isOptionalType()) {
            return target;
        }

        if (expr->isUndefinedLiteralExpr()) {
            return target;
        }

        if (expr->isStringLiteralExpr() && target.removeOptional().isPointerType() && target.removeOptional().getPointee().isChar() &&
            !target.removeOptional().getPointee().isMutable()) {
            // Special case: allow passing string literals as C-strings (const char*).
            return target;
        }

        if (expr->isArrayLiteralExpr() && target.isArrayWithConstantSize()) {
            bool isConvertible = llvm::all_of(llvm::cast<ArrayLiteralExpr>(expr)->getElements(), [&](auto& element) {
                return isImplicitlyConvertible(element, source.getElementType(), target.getElementType());
            });

            if (isConvertible) {
                for (auto& element : llvm::cast<ArrayLiteralExpr>(expr)->getElements()) {
                    // FIXME: Don't set type here.
                    element->setType(target.getElementType());
                }
                return target;
            }
        }
    }

    if ((allowPointerToTemporary || (expr && expr->isLvalue())) && target.removeOptional().isPointerType() &&
        (source.isMutable() || !target.removeOptional().getPointee().isMutable()) &&
        isImplicitlyConvertible(expr, source, target.removeOptional().getPointee())) {
        return source;
    }

    if (source.isPointerType() && source.getPointee() == target) {
        // Auto-dereference.
        return target;
    }

    if (target.isOptionalType() && (!expr || !expr->isNullLiteralExpr()) && isImplicitlyConvertible(expr, source, target.getWrappedType())) {
        return source;
    }

    if (source.isArrayType() && target.removeOptional().isPointerType() &&
        isImplicitlyConvertible(nullptr, source.getElementType(), target.removeOptional().getPointee())) {
        return source;
    }

    if (source.isPointerType() && source.getPointee().isArrayWithConstantSize() &&
        (target.isArrayWithRuntimeSize() || target.isArrayWithUnknownSize()) && source.getPointee().getElementType() == target.getElementType()) {
        return source;
    }

    if (source.isPointerType() && source.getPointee().isArrayType() && target.removeOptional().isPointerType() &&
        isImplicitlyConvertible(nullptr, source.getPointee().getElementType(), target.removeOptional().getPointee())) {
        return source;
    }

    if (source.isArrayWithUnknownSize() && target.isPointerType() && (source.getElementType() == target.getPointee() || target.getPointee().isVoid())) {
        return source;
    }

    if (target.isArrayWithUnknownSize() && source.isPointerType() && target.getElementType() == source.getPointee()) {
        return source;
    }

    if (source.isTupleType() && target.isTupleType()) {
        auto* tupleExpr = llvm::dyn_cast_or_null<TupleExpr>(expr);
        auto sourceElements = source.getTupleElements();
        auto targetElements = target.getTupleElements();

        for (size_t i = 0; i < sourceElements.size(); ++i) {
            if (sourceElements[i].name != targetElements[i].name) {
                return Type();
            }

            auto* elementValue = tupleExpr ? tupleExpr->getElements()[i].getValue() : nullptr;

            if (!isImplicitlyConvertible(elementValue, sourceElements[i].type, targetElements[i].type)) {
                return Type();
            }
        }

        return target;
    }

    return Type();
}

static bool containsGenericParam(Type type, llvm::StringRef genericParam) {
    switch (type.getKind()) {
        case TypeKind::BasicType:
            for (Type genericArg : type.getGenericArgs()) {
                if (containsGenericParam(genericArg, genericParam)) {
                    return true;
                }
            }
            return type.getName() == genericParam;

        case TypeKind::ArrayType:
            return containsGenericParam(type.getElementType(), genericParam);

        case TypeKind::TupleType:
            llvm_unreachable("unimplemented");

        case TypeKind::FunctionType:
            for (Type paramType : type.getParamTypes()) {
                if (containsGenericParam(paramType, genericParam)) {
                    return true;
                }
            }
            return containsGenericParam(type.getReturnType(), genericParam);

        case TypeKind::PointerType:
            return containsGenericParam(type.getPointee(), genericParam);

        case TypeKind::OptionalType:
            return containsGenericParam(type.getWrappedType(), genericParam);
    }

    llvm_unreachable("all cases handled");
}

static Type findGenericArg(Type argType, Type paramType, llvm::StringRef genericParam) {
    if (paramType.isBasicType() && paramType.getName() == genericParam) {
        return argType;
    }

    switch (argType.getKind()) {
        case TypeKind::BasicType:
            if (!argType.getGenericArgs().empty() && paramType.isBasicType() && paramType.getName() == argType.getName()) {
                ASSERT(argType.getGenericArgs().size() == paramType.getGenericArgs().size());
                for (auto&& [argTypeGenericArg, paramTypeGenericArg] : llvm::zip_first(argType.getGenericArgs(), paramType.getGenericArgs())) {
                    if (Type type = findGenericArg(argTypeGenericArg, paramTypeGenericArg, genericParam)) {
                        return type;
                    }
                }
            }
            break;

        case TypeKind::ArrayType:
            if (paramType.isArrayType()) {
                return findGenericArg(argType.getElementType(), paramType.getElementType(), genericParam);
            }
            break;

        case TypeKind::TupleType:
            llvm_unreachable("unimplemented");

        case TypeKind::FunctionType:
            if (paramType.isFunctionType()) {
                for (auto&& [argTypeParamType, paramTypeParamTypes] : llvm::zip_first(argType.getParamTypes(), paramType.getParamTypes())) {
                    if (Type type = findGenericArg(argTypeParamType, paramTypeParamTypes, genericParam)) {
                        return type;
                    }
                }
                return findGenericArg(argType.getReturnType(), paramType.getReturnType(), genericParam);
            }
            break;

        case TypeKind::PointerType:
            if (paramType.isPointerType()) {
                return findGenericArg(argType.getPointee(), paramType.getPointee(), genericParam);
            }
            break;

        case TypeKind::OptionalType:
            if (paramType.isOptionalType()) {
                return findGenericArg(argType.getWrappedType(), paramType.getWrappedType(), genericParam);
            }
            break;
    }

    if (paramType.removeOptional().isPointerType()) {
        return findGenericArg(argType, paramType.removeOptional().getPointee(), genericParam);
    }

    return Type();
}

std::vector<Type> Typechecker::inferGenericArgsFromCallArgs(llvm::ArrayRef<GenericParamDecl> genericParams, CallExpr& call,
                                                            llvm::ArrayRef<ParamDecl> params, bool returnOnError) {
    if (call.getArgs().size() != params.size()) return {};

    std::vector<Type> inferredGenericArgs;

    for (auto& genericParam : genericParams) {
        Type genericArg;
        Expr* genericArgValue;

        for (auto&& [param, arg] : llvm::zip_first(params, call.getArgs())) {
            Type paramType = param.getType();

            if (containsGenericParam(paramType, genericParam.getName())) {
                // FIXME: The args will also be typechecked by validateArgs() after this function. Get rid of this duplicated typechecking.
                auto* argValue = arg.getValue();
                Type argType = typecheckExpr(*argValue);
                Type maybeGenericArg = findGenericArg(argType, paramType, genericParam.getName());
                if (!maybeGenericArg) continue;

                if (!genericArg) {
                    genericArg = maybeGenericArg;
                    genericArgValue = argValue;
                } else {
                    Type paramTypeWithGenericArg = paramType.resolve({ { genericParam.getName(), genericArg } });
                    Type paramTypeWithMaybeGenericArg = paramType.resolve({ { genericParam.getName(), maybeGenericArg } });

                    if (convert(argValue, paramTypeWithGenericArg, true)) {
                        continue;
                    } else if (convert(genericArgValue, paramTypeWithMaybeGenericArg, true)) {
                        genericArg = maybeGenericArg;
                        genericArgValue = argValue;
                    } else {
                        ERROR(call.getLocation(), "couldn't infer generic parameter '" << genericParam.getName() << "' of '"
                                                                                       << call.getFunctionName() << "' because of conflicting argument types '"
                                                                                       << genericArg << "' and '" << maybeGenericArg << "'");
                    }
                }
            }
        }

        if (genericArg) {
            inferredGenericArgs.push_back(genericArg);
        } else {
            return {};
        }
    }

    ASSERT(genericParams.size() == inferredGenericArgs.size());

    for (auto&& [genericParam, genericArg] : llvm::zip(genericParams, inferredGenericArgs)) {
        if (!genericParam.getConstraints().empty()) {
            ASSERT(genericParam.getConstraints().size() == 1, "cannot have multiple generic constraints yet");

            auto* interface = getTypeDecl(*llvm::cast<BasicType>(genericParam.getConstraints()[0].getBase()));
            std::string errorReason;

            if (genericArg.isBasicType()) {
                auto* typeDecl = getTypeDecl(*llvm::cast<BasicType>(genericArg.getBase()));

                if (!typeDecl || !typeDecl->hasInterface(*interface)) {
                    if (returnOnError) {
                        return {};
                    } else {
                        ERROR(call.getLocation(), "type '" << genericArg << "' doesn't implement interface '" << interface->getName() << "'");
                    }
                }
            }
        }
    }

    return inferredGenericArgs;
}

void delta::validateGenericArgCount(size_t genericParamCount, llvm::ArrayRef<Type> genericArgs, llvm::StringRef name, SourceLocation location) {
    if (genericArgs.size() < genericParamCount) {
        REPORT_ERROR(location, "too few generic arguments to '" << name << "', expected " << genericParamCount);
    } else if (genericArgs.size() > genericParamCount) {
        REPORT_ERROR(location, "too many generic arguments to '" << name << "', expected " << genericParamCount);
    }
}

static void validateArgCount(size_t paramCount, size_t argCount, bool isVariadic, llvm::StringRef name, SourceLocation location) {
    if (argCount < paramCount) {
        REPORT_ERROR(location, "too few arguments to '" << name << "', expected " << (isVariadic ? "at least " : "") << paramCount);
    } else if (!isVariadic && argCount > paramCount) {
        REPORT_ERROR(location, "too many arguments to '" << name << "', expected " << paramCount);
    }
}

llvm::StringMap<Type> Typechecker::getGenericArgsForCall(llvm::ArrayRef<GenericParamDecl> genericParams, CallExpr& call,
                                                         llvm::ArrayRef<ParamDecl> params, bool returnOnError, Type expectedType) {
    ASSERT(!genericParams.empty());
    std::vector<Type> inferredGenericArgs;
    llvm::ArrayRef<Type> genericArgTypes;

    if (call.getGenericArgs().empty()) {
        if (expectedType && expectedType.isBasicType()) {
            // TODO: Should probably check that expectedType is the same type as the type whose generics args we're inferring.
            genericArgTypes = expectedType.getGenericArgs();
        } else if (call.getArgs().empty()) {
            ERROR(call.getLocation(), "can't infer generic parameters, please specify them explicitly");
        } else {
            inferredGenericArgs = inferGenericArgsFromCallArgs(genericParams, call, params, returnOnError);
            if (inferredGenericArgs.empty()) return {};
            ASSERT(inferredGenericArgs.size() == genericParams.size());
            genericArgTypes = inferredGenericArgs;
        }
    } else {
        genericArgTypes = call.getGenericArgs();
    }

    llvm::StringMap<Type> genericArgs;
    auto genericArg = genericArgTypes.begin();

    for (const GenericParamDecl& genericParam : genericParams) {
        genericArgs.try_emplace(genericParam.getName(), *genericArg++);
    }

    return genericArgs;
}

Type Typechecker::typecheckBuiltinConversion(CallExpr& expr) {
    if (expr.getArgs().size() != 1) {
        ERROR(expr.getLocation(), "expected single argument to converting constructor");
    }
    if (!expr.getGenericArgs().empty()) {
        ERROR(expr.getLocation(), "expected no generic arguments to converting constructor");
    }
    if (!expr.getArgs().front().getName().empty()) {
        ERROR(expr.getLocation(), "expected unnamed argument to converting constructor");
    }

    auto sourceType = typecheckExpr(*expr.getArgs().front().getValue());
    auto targetType = BasicType::get(expr.getFunctionName(), {});

    if (sourceType == targetType) {
        WARN(expr.getCallee().getLocation(), "unnecessary conversion to same type");
    }

    expr.setType(targetType);
    return expr.getType();
}

static std::vector<Note> getCandidateNotes(llvm::ArrayRef<Decl*> candidates) {
    return map(candidates, [](Decl* candidate) { return Note{ candidate->getLocation(), "candidate function:" }; });
}

static bool isStdlibDecl(Decl* decl) {
    return decl->getModule() && decl->getModule()->getName() == "std";
}

static bool isCHeaderDecl(Decl* decl) {
    return decl->getModule() && decl->getModule()->getName().endswith_lower(".h");
}

static bool equals(const llvm::StringMap<Type>& a, const llvm::StringMap<Type>& b) {
    if (a.size() != b.size()) return false;

    for (auto& aEntry : a) {
        auto bEntry = b.find(aEntry.getKey());
        if (bEntry == b.end() || aEntry.getValue() != bEntry->getValue()) return false;
    }

    return true;
}

Decl* Typechecker::resolveOverload(llvm::ArrayRef<Decl*> decls, CallExpr& expr, llvm::StringRef callee, Type expectedType) {
    std::vector<Decl*> matches;
    std::vector<ConstructorDecl*> constructorDecls;
    bool isInitCall = false;

    for (Decl* decl : decls) {
        switch (decl->getKind()) {
            case DeclKind::FunctionTemplate: {
                auto* functionTemplate = llvm::cast<FunctionTemplate>(decl);
                auto genericParams = functionTemplate->getGenericParams();

                if (!expr.getGenericArgs().empty() && expr.getGenericArgs().size() != genericParams.size()) {
                    if (decls.size() == 1) {
                        validateGenericArgCount(genericParams.size(), expr.getGenericArgs(), expr.getFunctionName(), expr.getLocation());
                    }
                    continue;
                }

                auto params = functionTemplate->getFunctionDecl()->getParams();
                auto genericArgs = getGenericArgsForCall(genericParams, expr, params, decls.size() != 1, expectedType);
                if (genericArgs.empty()) continue; // Couldn't infer generic arguments.

                auto* functionDecl = functionTemplate->instantiate(genericArgs);

                if (decls.size() == 1) {
                    validateArgs(expr, *functionDecl, callee, expr.getCallee().getLocation());
                    declsToTypecheck.emplace_back(functionDecl);
                    return functionDecl;
                }
                if (argumentsMatch(expr, functionDecl)) {
                    declsToTypecheck.emplace_back(functionDecl); // TODO: Do this only after the final match has been selected.
                    matches.push_back(functionDecl);
                }
                break;
            }
            case DeclKind::FunctionDecl:
            case DeclKind::MethodDecl:
            case DeclKind::ConstructorDecl: {
                auto& functionDecl = llvm::cast<FunctionDecl>(*decl);

                if (decls.size() == 1) {
                    validateGenericArgCount(0, expr.getGenericArgs(), expr.getFunctionName(), expr.getLocation());
                    validateArgs(expr, functionDecl, callee, expr.getCallee().getLocation());
                    return &functionDecl;
                }
                if (argumentsMatch(expr, &functionDecl)) {
                    matches.push_back(&functionDecl);
                }
                break;
            }
            case DeclKind::TypeDecl: {
                auto* typeDecl = llvm::cast<TypeDecl>(decl);
                isInitCall = true;
                validateGenericArgCount(0, expr.getGenericArgs(), expr.getFunctionName(), expr.getLocation());
                constructorDecls = typeDecl->getConstructors();
                ASSERT(!constructorDecls.empty());
                ASSERT(decls.size() == 1);
                decls = llvm::ArrayRef(reinterpret_cast<Decl**>(constructorDecls.data()), constructorDecls.size());

                for (auto* constructorDecl : constructorDecls) {
                    if (constructorDecls.size() == 1) {
                        validateArgs(expr, *constructorDecl, callee, expr.getCallee().getLocation());
                        return constructorDecl;
                    }
                    if (argumentsMatch(expr, constructorDecl)) {
                        matches.push_back(constructorDecl);
                    }
                }
                break;
            }
            case DeclKind::TypeTemplate: {
                auto* typeTemplate = llvm::cast<TypeTemplate>(decl);
                isInitCall = true;
                constructorDecls = typeTemplate->getTypeDecl()->getConstructors();
                ASSERT(decls.size() == 1);
                decls = llvm::ArrayRef(reinterpret_cast<Decl**>(constructorDecls.data()), constructorDecls.size());

                std::vector<llvm::StringMap<Type>> genericArgSets;

                for (auto* constructorDecl : constructorDecls) {
                    auto params = constructorDecl->getParams();
                    auto genericArgs = getGenericArgsForCall(typeTemplate->getGenericParams(), expr, params, constructorDecls.size() != 1, expectedType);
                    if (genericArgs.empty()) continue; // Couldn't infer generic arguments.
                    if (llvm::find_if(genericArgSets, [&](auto& set) { return equals(set, genericArgs); }) == genericArgSets.end()) {
                        genericArgSets.push_back(genericArgs);
                    }
                }

                for (auto& genericArgs : genericArgSets) {
                    TypeDecl* typeDecl = nullptr;

                    auto genericArgTypes = map(genericArgs, [](auto& entry) { return entry.getValue(); });
                    auto typeDecls = findDecls(getQualifiedTypeName(typeTemplate->getTypeDecl()->getName(), genericArgTypes));

                    if (typeDecls.empty()) {
                        typeDecl = typeTemplate->instantiate(genericArgs);
                        getCurrentModule()->addToSymbolTable(*typeDecl);
                        declsToTypecheck.push_back(typeDecl);
                    } else {
                        typeDecl = llvm::cast<TypeDecl>(typeDecls[0]);
                    }

                    for (auto* constructorDecl : typeDecl->getConstructors()) {
                        if (constructorDecls.size() == 1) {
                            validateArgs(expr, *constructorDecl, callee, expr.getCallee().getLocation());
                            return constructorDecl;
                        }
                        if (argumentsMatch(expr, constructorDecl)) {
                            matches.push_back(constructorDecl);
                        }
                    }
                }
                break;
            }
            case DeclKind::VarDecl:
            case DeclKind::ParamDecl:
            case DeclKind::FieldDecl: {
                auto* variableDecl = llvm::cast<VariableDecl>(decl);

                if (auto* functionType = llvm::dyn_cast<FunctionType>(variableDecl->getType().getBase())) {
                    auto paramDecls = functionType->getParamDecls(variableDecl->getLocation());

                    if (decls.size() == 1) {
                        validateArgs(expr, paramDecls, false, callee, expr.getCallee().getLocation());
                        return variableDecl;
                    }
                    if (argumentsMatch(expr, nullptr, paramDecls)) {
                        matches.push_back(variableDecl);
                    }
                }
                break;
            }
            case DeclKind::DestructorDecl:
                matches.push_back(decl);
                break;

            default:
                continue;
        }
    }

    if (matches.size() > 1) {
        // Prefer stdlib candidate if there's exactly one of them and all the others are from C headers.
        if (llvm::count_if(matches, isStdlibDecl) == 1 &&
            llvm::all_of(matches, [](Decl* decl) { return isStdlibDecl(decl) || isCHeaderDecl(decl); })) {
            matches = { *llvm::find_if(matches, isStdlibDecl) };
        } else {
            ERROR_WITH_NOTES(expr.getCallee().getLocation(), getCandidateNotes(decls),
                             "ambiguous reference to '" << callee << "'" << (isInitCall ? " constructor" : ""));
        }
    }

    if (matches.size() == 1) {
        validateArgs(expr, *matches.front());
        return matches.front();
    }

    if (decls.empty()) {
        ERROR(expr.getCallee().getLocation(), "unknown identifier '" << callee << "'");
    }

    bool atLeastOneFunction = llvm::any_of(decls, [](Decl* decl) {
        return decl->isFunctionDecl() || decl->isFunctionTemplate() || decl->isTypeDecl() || decl->isTypeTemplate();
    });

    if (atLeastOneFunction) {
        auto argTypeStrings = map(expr.getArgs(), [&](auto& arg) {
            auto type = typecheckExpr(*arg.getValue());
            return type ? type.toString(true) : "???";
        });

        ERROR_WITH_NOTES(expr.getCallee().getLocation(), getCandidateNotes(decls),
                         "no matching " << (isInitCall ? "constructor for '" : "function for call to '") << callee
                                        << "' with argument list of type '(" << llvm::join(argTypeStrings, ", ") << ")'");
    } else {
        ERROR(expr.getCallee().getLocation(), "'" << callee << "' is not a function");
    }
}

std::vector<Decl*> Typechecker::findCalleeCandidates(const CallExpr& expr, llvm::StringRef callee) {
    TypeDecl* receiverTypeDecl;

    if (expr.getReceiverType() && expr.getReceiverType().removePointer().isBasicType()) {
        receiverTypeDecl = getTypeDecl(*llvm::cast<BasicType>(expr.getReceiverType().removePointer().getBase()));
    } else {
        receiverTypeDecl = nullptr;
    }

    return findDecls(callee, receiverTypeDecl, isPostProcessing);
}

Type Typechecker::typecheckCallExpr(CallExpr& expr, Type expectedType) {
    if (!expr.callsNamedFunction()) {
        ERROR(expr.getLocation(), "anonymous function calls not implemented yet");
    }

    if (Type::isBuiltinScalar(expr.getFunctionName())) {
        return typecheckBuiltinConversion(expr);
    }

    if (expr.isBuiltinCast()) {
        return typecheckBuiltinCast(expr);
    }

    if (expr.getFunctionName() == "assert") {
        ParamDecl assertParam(Type::getBool(), "", false, SourceLocation());
        validateArgs(expr, assertParam, false, expr.getFunctionName(), expr.getLocation());
        validateGenericArgCount(0, expr.getGenericArgs(), expr.getFunctionName(), expr.getLocation());
        return Type::getVoid();
    }

    Decl* decl;

    if (auto* enumCase = getEnumCase(expr.getCallee())) {
        decl = enumCase;
        llvm::cast<MemberExpr>(expr.getCallee()).setDecl(*decl);
    } else if (expr.getCallee().isMemberExpr()) {
        Type receiverType = typecheckExpr(*expr.getReceiver());
        expr.setReceiverType(receiverType);

        if (receiverType.isOptionalType()) {
            WARN(expr.getReceiver()->getLocation(), "calling member function through value of optional type '"
                                                        << receiverType << "' which may be null; "
                                                        << "unwrap the value with a postfix '!' to silence this warning");
        } else if (receiverType.removePointer().isArrayType()) {
            // TODO: Move these member functions to a 'struct Array' declaration in stdlib.
            if (expr.getFunctionName() == "data") {
                validateArgs(expr, {}, false, expr.getFunctionName(), expr.getLocation());
                validateGenericArgCount(0, expr.getGenericArgs(), expr.getFunctionName(), expr.getLocation());
                return ArrayType::get(receiverType.removePointer().getElementType(), ArrayType::unknownSize);
            }
            if (expr.getFunctionName() == "size") {
                validateArgs(expr, {}, false, expr.getFunctionName(), expr.getLocation());
                validateGenericArgCount(0, expr.getGenericArgs(), expr.getFunctionName(), expr.getLocation());
                return ArrayType::getIndexType();
            }
            if (expr.getFunctionName() == "iterator") {
                validateArgs(expr, {}, false, expr.getFunctionName(), expr.getLocation());
                validateGenericArgCount(0, expr.getGenericArgs(), expr.getFunctionName(), expr.getLocation());
                return BasicType::get("ArrayIterator", receiverType.removePointer().getElementType());
            }

            ERROR(expr.getReceiver()->getLocation(),
                  "type '" << receiverType.removePointer() << "' has no member function '" << expr.getFunctionName() << "'");
        } else if (receiverType.removePointer().isBuiltinType() && expr.getFunctionName() == "deinit") {
            return Type::getVoid();
        }

        if (expr.getArgs().size() == 1 && expr.getFunctionName() == "init") {
            typecheckExpr(*expr.getArgs()[0].getValue());

            if (expr.isMoveInit()) {
                if (!expr.getArgs()[0].getValue()->getType().isImplicitlyCopyable()) {
                    setMoved(expr.getArgs()[0].getValue(), true);
                }
                return Type::getVoid();
            }
        }

        auto callee = expr.getQualifiedFunctionName();
        auto decls = findCalleeCandidates(expr, callee);

        if (decls.empty() && expr.getFunctionName() == "deinit") {
            return Type::getVoid();
        }

        decl = resolveOverload(decls, expr, callee, expectedType);
    } else {
        auto callee = expr.getFunctionName();
        auto decls = findCalleeCandidates(expr, callee);
        decl = resolveOverload(decls, expr, callee, expectedType);

        if (auto* constructorDecl = llvm::dyn_cast<ConstructorDecl>(decl)) {
            expr.setReceiverType(constructorDecl->getTypeDecl()->getType());
        } else if (decl->isMethodDecl()) {
            auto* varDecl = llvm::cast<VarDecl>(findDecl("this", expr.getCallee().getLocation()));
            expr.setReceiverType(varDecl->getType());
        }
    }

    checkHasAccess(*decl, expr.getCallee().getLocation(), AccessLevel::None);
    if (auto* constructorDecl = llvm::dyn_cast<ConstructorDecl>(decl)) {
        checkHasAccess(*constructorDecl->getTypeDecl(), expr.getCallee().getLocation(), AccessLevel::None);
    }

    std::vector<ParamDecl> params;

    switch (decl->getKind()) {
        case DeclKind::FunctionDecl:
        case DeclKind::MethodDecl:
        case DeclKind::ConstructorDecl:
        case DeclKind::DestructorDecl:
            params = llvm::cast<FunctionDecl>(decl)->getParams();
            break;

        case DeclKind::VarDecl:
        case DeclKind::ParamDecl:
        case DeclKind::FieldDecl: {
            auto type = llvm::cast<VariableDecl>(decl)->getType();
            params = llvm::cast<FunctionType>(type.getBase())->getParamDecls();
            break;
        }
        case DeclKind::EnumCase: {
            auto type = llvm::cast<EnumCase>(decl)->getAssociatedType();
            params = map(type.getTupleElements(), [&](auto& e) { return ParamDecl(e.type, std::string(e.name), false, decl->getLocation()); });
            validateArgs(expr, params, false, decl->getName(), expr.getLocation());
            break;
        }
        default:
            llvm_unreachable("invalid callee decl");
    }

    for (auto&& [param, arg] : llvm::zip_first(params, expr.getArgs())) {
        if (!param.getType().isImplicitlyCopyable()) {
            setMoved(arg.getValue(), true);
        }
    }

    expr.setCalleeDecl(decl);
    decl->setReferenced(true);

    switch (decl->getKind()) {
        case DeclKind::FunctionDecl:
        case DeclKind::MethodDecl:
        case DeclKind::DestructorDecl:
            return llvm::cast<FunctionDecl>(decl)->getFunctionType()->getReturnType();

        case DeclKind::ConstructorDecl: {
            auto constructorDecl = llvm::cast<ConstructorDecl>(decl);
            if (constructorDecl->getTypeDecl()->isInterface()) {
                typecheckFunctionDecl(*constructorDecl);
            }
            return llvm::cast<ConstructorDecl>(decl)->getTypeDecl()->getType();
        }
        case DeclKind::VarDecl:
        case DeclKind::ParamDecl:
        case DeclKind::FieldDecl:
            return llvm::cast<FunctionType>(llvm::cast<VariableDecl>(decl)->getType().getBase())->getReturnType();

        case DeclKind::EnumCase:
            return llvm::cast<EnumCase>(decl)->getType();

        default:
            llvm_unreachable("invalid callee decl");
    }
}

bool Typechecker::argumentsMatch(CallExpr& expr, const FunctionDecl* functionDecl, llvm::ArrayRef<ParamDecl> params) {
    if (functionDecl) params = functionDecl->getParams();
    bool isVariadic = functionDecl && functionDecl->isVariadic();
    auto args = expr.getArgs();

    if (args.size() < params.size()) {
        return false;
    }

    if (!isVariadic && args.size() > params.size()) {
        return false;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        auto& arg = args[i];
        auto* param = i < params.size() ? &params[i] : nullptr;

        if (!arg.getName().empty() && (!param || arg.getName() != param->getName())) {
            return false;
        }

        if (param && !isImplicitlyConvertible(arg.getValue(), typecheckExpr(*arg.getValue()), param->getType(), true)) {
            return false;
        }
    }

    return true;
}

void Typechecker::validateArgs(CallExpr& expr, const Decl& calleeDecl, llvm::StringRef functionName, SourceLocation location) {
    switch (calleeDecl.getKind()) {
        case DeclKind::FunctionDecl:
        case DeclKind::MethodDecl:
        case DeclKind::ConstructorDecl:
        case DeclKind::DestructorDecl: {
            auto& functionDecl = llvm::cast<FunctionDecl>(calleeDecl);
            validateArgs(expr, functionDecl.getParams(), functionDecl.isVariadic(), functionName, location);
            break;
        }
        case DeclKind::VarDecl:
        case DeclKind::ParamDecl:
        case DeclKind::FieldDecl: {
            auto* variableDecl = llvm::cast<VariableDecl>(&calleeDecl);
            if (auto* functionType = llvm::dyn_cast<FunctionType>(variableDecl->getType().getBase())) {
                auto paramDecls = functionType->getParamDecls(variableDecl->getLocation());
                validateArgs(expr, paramDecls, false, functionName, location);
            }
            break;
        }
        default:
            llvm_unreachable("invalid callee decl");
    }
}

void Typechecker::validateArgs(CallExpr& expr, llvm::ArrayRef<ParamDecl> params, bool isVariadic, llvm::StringRef functionName, SourceLocation location) {
    auto args = expr.getArgs();
    validateArgCount(params.size(), args.size(), isVariadic, functionName, location);

    for (size_t i = 0; i < args.size(); ++i) {
        auto& arg = args[i];
        auto* param = i < params.size() ? &params[i] : nullptr;

        if (!arg.getName().empty() && (!param || arg.getName() != param->getName())) {
            ERROR(arg.getLocation(), "invalid argument name '" << arg.getName() << "' for parameter '" << param->getName() << "'");
        }

        typecheckExpr(*arg.getValue(), false, param ? param->getType() : Type());
        if (param && !convert(arg.getValue(), param->getType(), true)) {
            ERROR(arg.getLocation(), "invalid argument #" << (i + 1) << " type '" << arg.getValue()->getType() << "' to '" << functionName
                                                          << "', expected '" << param->getType() << "'");
        }
    }
}

static bool isValidCast(Type sourceType, Type targetType) {
    switch (sourceType.getKind()) {
        case TypeKind::BasicType:
        case TypeKind::TupleType:
        case TypeKind::FunctionType:
            return false;

        case TypeKind::PointerType: {
            Type sourcePointee = sourceType.getPointee();

            if (targetType.isPointerType()) {
                Type targetPointee = targetType.getPointee();

                if (sourcePointee.isVoid() && (!targetPointee.isMutable() || sourcePointee.isMutable())) {
                    return true;
                } else if (targetPointee.isVoid() && (!targetPointee.isMutable() || sourcePointee.isMutable())) {
                    return true;
                } else if (targetPointee.isArrayWithConstantSize() && sourcePointee == targetPointee.getElementType()) {
                    return true;
                }
            } else if (targetType.isArrayWithUnknownSize()) {
                if (sourcePointee.isVoid() && (!targetType.getElementType().isMutable() || sourcePointee.isMutable())) {
                    return true;
                } else if (sourcePointee == targetType.getElementType()) {
                    return true;
                }
            }

            return false;
        }
        case TypeKind::ArrayType: {
            if (targetType.isPointerType()) {
                Type targetPointee = targetType.getPointee();

                if (targetPointee.isVoid() && (!targetPointee.isMutable() || sourceType.getElementType().isMutable())) {
                    return true;
                }
            }

            return false;
        }
        case TypeKind::OptionalType: {
            Type sourceWrappedType = sourceType.getWrappedType();

            if (sourceWrappedType.isPointerType() && targetType.isOptionalType()) {
                Type targetWrappedType = targetType.getWrappedType();

                if (targetWrappedType.isPointerType() && isValidCast(sourceWrappedType, targetWrappedType)) {
                    return true;
                }
            }

            return false;
        }
    }

    llvm_unreachable("all cases handled");
}

Type Typechecker::typecheckBuiltinCast(CallExpr& expr) {
    validateGenericArgCount(1, expr.getGenericArgs(), expr.getFunctionName(), expr.getLocation());
    validateArgCount(1, expr.getArgs().size(), false, expr.getFunctionName(), expr.getLocation());

    Type sourceType = typecheckExpr(*expr.getArgs().front().getValue());
    Type targetType = expr.getGenericArgs().front();

    if (!isValidCast(sourceType, targetType)) {
        ERROR(expr.getCallee().getLocation(), "illegal cast from '" << sourceType << "' to '" << targetType << "'");
    }

    return targetType;
}

Type Typechecker::typecheckSizeofExpr(SizeofExpr&) {
    return Type::getUInt64();
}

Type Typechecker::typecheckAddressofExpr(AddressofExpr& expr) {
    if (!typecheckExpr(expr.getOperand()).removeOptional().isPointerType()) {
        ERROR(expr.getLocation(), "operand to 'addressof' must have pointer type");
    }
    return Type::getUIntPtr();
}

Type Typechecker::typecheckMemberExpr(MemberExpr& expr) {
    if (auto* enumCase = getEnumCase(expr)) {
        checkHasAccess(*enumCase->getEnumDecl(), expr.getBaseExpr()->getLocation(), AccessLevel::None);
        expr.setDecl(*enumCase);
        return enumCase->getType();
    }

    Type baseType = typecheckExpr(*expr.getBaseExpr());

    if (baseType.isOptionalType()) {
        WARN(expr.getBaseExpr()->getLocation(), "accessing member through value of optional type '"
                                                    << baseType << "' which may be null; "
                                                    << "unwrap the value with a postfix '!' to silence this warning");
        baseType = baseType.getWrappedType();
    }

    if (baseType.isPointerType()) {
        baseType = baseType.getPointee();
    }

    if (baseType.isArrayType()) {
        auto sizeSynonyms = { "count", "length", "size" };

        if (llvm::is_contained(sizeSynonyms, expr.getMemberName())) {
            ERROR(expr.getLocation(), "use the '.size()' member function to get the number of elements in an array");
        }
    }

    if (auto* typeDecl = baseType.getDecl()) {
        for (auto& field : typeDecl->getFields()) {
            if (field.getName() == expr.getMemberName()) {
                checkHasAccess(field, expr.getLocation(), AccessLevel::None);
                expr.setDecl(field);
                return field.getType().withMutability(baseType.getMutability());
            }
        }
    }

    if (baseType.isTupleType()) {
        for (auto& element : baseType.getTupleElements()) {
            if (element.name == expr.getMemberName()) {
                return element.type;
            }
        }
    }

    ERROR(expr.getLocation(), "no member named '" << expr.getMemberName() << "' in '" << baseType << "'");
}

Type Typechecker::typecheckIndexExpr(IndexExpr& expr) {
    Type lhsType = typecheckExpr(*expr.getBase());
    Type arrayType;

    if (lhsType.isArrayType()) {
        arrayType = lhsType;
    } else if (lhsType.isPointerType() && lhsType.getPointee().isArrayType()) {
        arrayType = lhsType.getPointee();
    } else if (lhsType.removePointer().isBuiltinType()) {
        ERROR(expr.getLocation(), "'" << lhsType << "' doesn't provide an index operator");
    } else {
        return typecheckCallExpr(expr);
    }

    Expr* indexExpr = expr.getIndex();
    Type indexType = typecheckExpr(*indexExpr);

    if (!convert(indexExpr, ArrayType::getIndexType())) {
        ERROR(indexExpr->getLocation(), "illegal index type '" << indexType << "', expected '" << ArrayType::getIndexType() << "'");
    }

    if (arrayType.isArrayWithConstantSize()) {
        if (indexExpr->isConstant()) {
            auto index = indexExpr->getConstantIntegerValue();

            if (index < 0 || index >= arrayType.getArraySize()) {
                ERROR(indexExpr->getLocation(), "accessing array out-of-bounds with index " << index << ", array size is " << arrayType.getArraySize());
            }
        }
    }

    return arrayType.getElementType();
}

Type Typechecker::typecheckUnwrapExpr(UnwrapExpr& expr) {
    Type type = typecheckExpr(expr.getOperand());
    if (!type.isOptionalType()) {
        ERROR(expr.getLocation(), "cannot unwrap non-optional type '" << type << "'");
    }
    return type.getWrappedType();
}

Type Typechecker::typecheckLambdaExpr(LambdaExpr& expr, Type expectedType) {
    for (size_t i = 0, e = expr.getFunctionDecl()->getParams().size(); i < e; ++i) {
        auto& param = expr.getFunctionDecl()->getParams()[i];
        if (!param.getType()) {
            auto inferredType = expectedType ? expectedType.getParamTypes()[i] : Type();
            if (!inferredType) {
                ERROR(param.getLocation(), "couldn't infer type for parameter '" << param.getName() << "'");
            }
            param.setType(inferredType);
        }
    }

    typecheckFunctionDecl(*expr.getFunctionDecl());
    return Type(expr.getFunctionDecl()->getFunctionType(), Mutability::Mutable, expr.getLocation());
}

Type Typechecker::typecheckIfExpr(IfExpr& expr) {
    auto conditionType = typecheckExpr(*expr.getCondition());

    if (!conditionType.isBool() && !conditionType.isOptionalType()) {
        ERROR(expr.getCondition()->getLocation(), "if-expression condition must have type 'bool' or optional type");
    }

    auto thenType = typecheckExpr(*expr.getThenExpr());
    auto elseType = typecheckExpr(*expr.getElseExpr());

    if (convert(expr.getElseExpr(), thenType)) {
        return thenType;
    } else if (convert(expr.getThenExpr(), elseType)) {
        return elseType;
    } else {
        ERROR(expr.getLocation(), "incompatible operand types ('" << thenType << "' and '" << elseType << "')");
    }
}

Type Typechecker::typecheckExpr(Expr& expr, bool useIsWriteOnly, Type expectedType) {
    if (expr.hasType()) {
        return expr.getType();
    }

    Type type;

    switch (expr.getKind()) {
        case ExprKind::VarExpr:
            type = typecheckVarExpr(llvm::cast<VarExpr>(expr), useIsWriteOnly);
            if (!type) throw CompileError();
            break;
        case ExprKind::StringLiteralExpr:
            type = typecheckStringLiteralExpr(llvm::cast<StringLiteralExpr>(expr));
            break;
        case ExprKind::CharacterLiteralExpr:
            type = typecheckCharacterLiteralExpr(llvm::cast<CharacterLiteralExpr>(expr));
            break;
        case ExprKind::IntLiteralExpr:
            type = typecheckIntLiteralExpr(llvm::cast<IntLiteralExpr>(expr));
            break;
        case ExprKind::FloatLiteralExpr:
            type = typecheckFloatLiteralExpr(llvm::cast<FloatLiteralExpr>(expr));
            break;
        case ExprKind::BoolLiteralExpr:
            type = typecheckBoolLiteralExpr(llvm::cast<BoolLiteralExpr>(expr));
            break;
        case ExprKind::NullLiteralExpr:
            type = typecheckNullLiteralExpr(llvm::cast<NullLiteralExpr>(expr));
            break;
        case ExprKind::UndefinedLiteralExpr:
            type = typecheckUndefinedLiteralExpr(llvm::cast<UndefinedLiteralExpr>(expr));
            break;
        case ExprKind::ArrayLiteralExpr:
            type = typecheckArrayLiteralExpr(llvm::cast<ArrayLiteralExpr>(expr), expectedType);
            break;
        case ExprKind::TupleExpr:
            type = typecheckTupleExpr(llvm::cast<TupleExpr>(expr));
            break;
        case ExprKind::UnaryExpr:
            type = typecheckUnaryExpr(llvm::cast<UnaryExpr>(expr));
            break;
        case ExprKind::BinaryExpr:
            type = typecheckBinaryExpr(llvm::cast<BinaryExpr>(expr));
            break;
        case ExprKind::CallExpr:
            type = typecheckCallExpr(llvm::cast<CallExpr>(expr), expectedType);
            break;
        case ExprKind::SizeofExpr:
            type = typecheckSizeofExpr(llvm::cast<SizeofExpr>(expr));
            break;
        case ExprKind::AddressofExpr:
            type = typecheckAddressofExpr(llvm::cast<AddressofExpr>(expr));
            break;
        case ExprKind::MemberExpr:
            type = typecheckMemberExpr(llvm::cast<MemberExpr>(expr));
            break;
        case ExprKind::IndexExpr:
            type = typecheckIndexExpr(llvm::cast<IndexExpr>(expr));
            break;
        case ExprKind::UnwrapExpr:
            type = typecheckUnwrapExpr(llvm::cast<UnwrapExpr>(expr));
            break;
        case ExprKind::LambdaExpr:
            type = typecheckLambdaExpr(llvm::cast<LambdaExpr>(expr), expectedType);
            break;
        case ExprKind::IfExpr:
            type = typecheckIfExpr(llvm::cast<IfExpr>(expr));
            break;
    }

    if (!useIsWriteOnly && type.isOptionalType() && isGuaranteedNonNull(expr)) {
        expr.setType(type.removeOptional());
    } else {
        expr.setType(type);
    }

    expr.setAssignableType(type);
    return expr.getType();
}

EnumCase* Typechecker::getEnumCase(const Expr& expr) {
    if (auto* memberExpr = llvm::dyn_cast<MemberExpr>(&expr)) {
        if (auto* varExpr = llvm::dyn_cast<VarExpr>(memberExpr->getBaseExpr())) {
            auto decls = findDecls(varExpr->getIdentifier());
            if (decls.size() == 1) {
                if (auto* enumDecl = llvm::dyn_cast<EnumDecl>(decls.front())) {
                    auto* enumCase = enumDecl->getCaseByName(memberExpr->getMemberName());
                    if (!enumCase) {
                        ERROR(expr.getLocation(), "enum '" << enumDecl->getName() << "' has no case named '" << memberExpr->getMemberName() << "'");
                    }
                    return enumCase;
                }
            }
        }
    }

    return nullptr;
}

void Typechecker::setMoved(Expr* expr, bool isMoved) {
    if (auto* varExpr = llvm::dyn_cast<VarExpr>(expr)) {
        ASSERT(varExpr->getDecl());

        if (isMoved) {
            movedDecls.insert(varExpr->getDecl());
        } else {
            movedDecls.erase(varExpr->getDecl());
        }
    }
}

void Typechecker::checkNotMoved(const Decl& decl, const VarExpr& expr) {
    if (movedDecls.count(&decl)) {
        ERROR(expr.getLocation(), "use of moved value '" << expr.getIdentifier() << "'");
    }
}
