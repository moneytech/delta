#include "operators.h"

bool BinaryOperator::isComparisonOperator() const {
    switch (rawValue) {
        case EQ: case NE: case LT: case LE: case GT: case GE: return true;
        case PLUS: case MINUS: case ASTERISK: case SLASH: return false;
    }
    assert(false);
}

std::ostream& operator<<(std::ostream& out, PrefixOperator op) {
    switch (op.rawValue) {
        case PLUS:  return out << "+";
        case MINUS: return out << "-";
        default: assert(false);
    }
}

std::ostream& operator<<(std::ostream& out, BinaryOperator op) {
    switch (op.rawValue) {
        case EQ: return out << "==";
        case NE: return out << "!=";
        case LT: return out << "<";
        case LE: return out << "<=";
        case GT: return out << ">";
        case GE: return out << ">=";
        case PLUS:  return out << "+";
        case MINUS: return out << "-";
        case ASTERISK: return out << "*";
        case SLASH:    return out << "/";
        default: assert(false);
    }
}