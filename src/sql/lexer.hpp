#pragma once

#include "common/types.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <cctype>

namespace felwood {
    enum class TokenType {
        CREATE, TABLE, INSERT, INTO, VALUES, SELECT, FROM, WHERE, GROUP, ORDER, BY, AS, AND, ASC, DESC,
        SUM, COUNT, MIN, MAX, AVG,
        KW_INT64, KW_FLOAT64, KW_STRING, KW_BOOLEAN,
        EQ, NEQ, LT, GT, LEQ, GEQ,
        LPAREN, RPAREN, COMMA, SEMICOLON, STAR,
        INT_LIT, FLOAT_LIT, STRING_LIT,
        IDENT,
        EOF_TOK,
    };

    struct Token {
        TokenType   type;
        std::string text;
        Value       literal{int64_t(0)};
    };

    class Lexer {
    public:
        explicit Lexer(std::string src) : src_(std::move(src)), pos_(0) {}

        std::vector<Token> tokenize() {
            std::vector<Token> tokens;
            while (true) {
                skip_whitespace();
                if (pos_ >= src_.size()) {
                    tokens.push_back({TokenType::EOF_TOK, ""});
                    break;
                }
                tokens.push_back(next_token());
            }
            return tokens;
        }

    private:
        std::string src_;
        std::size_t pos_;

        void skip_whitespace() {
            while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_])))
                ++pos_;
        }

        Token next_token() {
            char c = src_[pos_];
            if (c == '\'')                                               return read_string();
            if (std::isdigit(static_cast<unsigned char>(c)))             return read_number();
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return read_ident_or_keyword();
            return read_symbol();
        }

        Token read_string() {
            ++pos_;
            std::string val;
            while (pos_ < src_.size() && src_[pos_] != '\'') val += src_[pos_++];
            if (pos_ < src_.size()) ++pos_;
            return {TokenType::STRING_LIT, "'" + val + "'", Value{val}};
        }

        Token read_number() {
            std::string num;
            bool is_float = false;
            while (pos_ < src_.size() &&
                   (std::isdigit(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.')) {
                if (src_[pos_] == '.') is_float = true;
                num += src_[pos_++];
                   }
            if (is_float)
                return {TokenType::FLOAT_LIT, num, Value{std::stod(num)}};
            return {TokenType::INT_LIT, num, Value{static_cast<int64_t>(std::stoll(num))}};
        }

        Token read_ident_or_keyword() {
            std::string word;
            while (pos_ < src_.size() &&
                   (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_'))
                word += src_[pos_++];

            std::string upper = word;
            for (auto& ch : upper)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

            static const std::unordered_map<std::string, TokenType> keywords = {
                {"CREATE",  TokenType::CREATE},  {"TABLE",   TokenType::TABLE},
                {"INSERT",  TokenType::INSERT},  {"INTO",    TokenType::INTO},
                {"VALUES",  TokenType::VALUES},  {"SELECT",  TokenType::SELECT},
                {"FROM",    TokenType::FROM},    {"WHERE",   TokenType::WHERE},
                {"GROUP",   TokenType::GROUP},   {"ORDER",   TokenType::ORDER},
                {"BY",      TokenType::BY},      {"ASC",     TokenType::ASC},
                {"DESC",    TokenType::DESC},
                {"AS",      TokenType::AS},      {"AND",     TokenType::AND},
                {"SUM",     TokenType::SUM},     {"COUNT",   TokenType::COUNT},
                {"MIN",     TokenType::MIN},     {"MAX",     TokenType::MAX},
                {"AVG",     TokenType::AVG},
                {"INT64",   TokenType::KW_INT64},   {"FLOAT64", TokenType::KW_FLOAT64},
                {"STRING",  TokenType::KW_STRING},  {"BOOLEAN", TokenType::KW_BOOLEAN},
            };

            auto it = keywords.find(upper);
            if (it != keywords.end()) return {it->second, word};
            return {TokenType::IDENT, word};
        }

        Token read_symbol() {
            char c = src_[pos_++];
            switch (c) {
                case '(': return {TokenType::LPAREN,    "("};
                case ')': return {TokenType::RPAREN,    ")"};
                case ',': return {TokenType::COMMA,     ","};
                case ';': return {TokenType::SEMICOLON, ";"};
                case '*': return {TokenType::STAR,      "*"};
                case '=': return {TokenType::EQ,        "="};
                case '<':
                    if (pos_ < src_.size() && src_[pos_] == '=') { ++pos_; return {TokenType::LEQ, "<="}; }
                    return {TokenType::LT, "<"};
                case '>':
                    if (pos_ < src_.size() && src_[pos_] == '=') { ++pos_; return {TokenType::GEQ, ">="}; }
                    return {TokenType::GT, ">"};
                case '!':
                    if (pos_ < src_.size() && src_[pos_] == '=') { ++pos_; return {TokenType::NEQ, "!="}; }
                    throw std::runtime_error("Lexer: unexpected character '!'");
                default:
                    throw std::runtime_error(std::string("Lexer: unexpected character '") + c + "'");
            }
        }
    };
}
