#pragma once

#include "sql/lexer.hpp"
#include "sql/ast.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace felwood {
    class Parser {
    public:
        explicit Parser(std::vector<Token> tokens)
            : tokens_(std::move(tokens)), pos_(0) {}

        Stmt parse() {
            if (peek().type == TokenType::CREATE) return parse_create();
            if (peek().type == TokenType::INSERT)  return parse_insert();
            if (peek().type == TokenType::SELECT)  return parse_select();
            throw std::runtime_error("Parser: expected CREATE, INSERT, or SELECT");
        }

    private:
        std::vector<Token> tokens_;
        std::size_t        pos_;

        Token& peek() { return tokens_[pos_]; }
        Token  consume() { return tokens_[pos_++]; }

        Token expect(TokenType t) {
            if (peek().type != t)
                throw std::runtime_error("Parser: unexpected token '" + peek().text + "'");
            return consume();
        }

        bool match(TokenType t) {
            if (peek().type == t) { ++pos_; return true; }
            return false;
        }

        CreateTableStmt parse_create() {
            expect(TokenType::CREATE);
            expect(TokenType::TABLE);
            std::string name = expect(TokenType::IDENT).text;
            expect(TokenType::LPAREN);

            std::vector<ColumnSchema> cols;
            do {
                std::string col_name = expect(TokenType::IDENT).text;
                DataType    col_type = parse_type();
                cols.push_back({col_name, col_type});
            } while (match(TokenType::COMMA));

            expect(TokenType::RPAREN);
            return {name, cols};
        }

        DataType parse_type() {
            switch (consume().type) {
                case TokenType::KW_INT64:   return DataType::INT64;
                case TokenType::KW_FLOAT64: return DataType::FLOAT64;
                case TokenType::KW_STRING:  return DataType::STRING;
                case TokenType::KW_BOOLEAN: return DataType::BOOLEAN;
                default: throw std::runtime_error("Parser: expected type name");
            }
        }

        InsertStmt parse_insert() {
            expect(TokenType::INSERT);
            expect(TokenType::INTO);
            std::string table = expect(TokenType::IDENT).text;
            expect(TokenType::VALUES);
            expect(TokenType::LPAREN);

            std::vector<Value> values;
            do {
                values.push_back(parse_literal());
            } while (match(TokenType::COMMA));

            expect(TokenType::RPAREN);
            return {table, values};
        }

        Value parse_literal() {
            Token t = consume();
            if (t.type == TokenType::INT_LIT   ||
                t.type == TokenType::FLOAT_LIT ||
                t.type == TokenType::STRING_LIT)
                return t.literal;
            throw std::runtime_error("Parser: expected literal, got '" + t.text + "'");
        }

        SelectStmt parse_select() {
            expect(TokenType::SELECT);
            auto items = parse_select_items();
            expect(TokenType::FROM);
            std::string from = expect(TokenType::IDENT).text;

            std::vector<BinaryExpr> where;
            if (match(TokenType::WHERE)) {
                do {
                    where.push_back(parse_condition());
                } while (match(TokenType::AND));
            }

            std::vector<std::string> group_by;
            if (peek().type == TokenType::GROUP) {
                consume();
                expect(TokenType::BY);
                do {
                    group_by.push_back(expect(TokenType::IDENT).text);
                } while (match(TokenType::COMMA));
            }

            return {items, from, where, group_by};
        }

        std::vector<SelectItem> parse_select_items() {
            std::vector<SelectItem> items;
            do {
                items.push_back(parse_select_item());
            } while (match(TokenType::COMMA));
            return items;
        }

        SelectItem parse_select_item() {
            std::optional<AggFunc> agg;
            std::string col;

            if (is_agg(peek().type)) {
                agg = to_agg(consume().type);
                expect(TokenType::LPAREN);
                col = expect(TokenType::IDENT).text;
                expect(TokenType::RPAREN);
            } else {
                col = expect(TokenType::IDENT).text;
            }

            std::string alias = col;
            if (match(TokenType::AS))
                alias = expect(TokenType::IDENT).text;

            return {agg, col, alias};
        }

        BinaryExpr parse_condition() {
            auto left  = parse_operand();
            auto op    = parse_op();
            auto right = parse_operand();
            return {op, left, right};
        }

        std::variant<ColumnRef, Literal> parse_operand() {
            TokenType t = peek().type;
            if (t == TokenType::IDENT)
                return ColumnRef{consume().text};
            if (t == TokenType::INT_LIT || t == TokenType::FLOAT_LIT || t == TokenType::STRING_LIT)
                return Literal{parse_literal()};
            throw std::runtime_error("Parser: expected column name or literal");
        }

        std::string parse_op() {
            Token t = consume();
            switch (t.type) {
                case TokenType::EQ:  return "=";
                case TokenType::NEQ: return "!=";
                case TokenType::LT:  return "<";
                case TokenType::GT:  return ">";
                case TokenType::LEQ: return "<=";
                case TokenType::GEQ: return ">=";
                default: throw std::runtime_error("Parser: expected comparison operator");
            }
        }

        static bool is_agg(TokenType t) {
            return t == TokenType::SUM   || t == TokenType::COUNT ||
                   t == TokenType::MIN   || t == TokenType::MAX   ||
                   t == TokenType::AVG;
        }

        static AggFunc to_agg(TokenType t) {
            switch (t) {
                case TokenType::SUM:   return AggFunc::SUM;
                case TokenType::COUNT: return AggFunc::COUNT;
                case TokenType::MIN:   return AggFunc::MIN;
                case TokenType::MAX:   return AggFunc::MAX;
                case TokenType::AVG:   return AggFunc::AVG;
                default: throw std::runtime_error("Parser: not an aggregate function");
            }
        }
    };
}
