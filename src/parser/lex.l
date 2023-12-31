    /* keywords are case insensitive */
%option caseless
    /* we don't need yywrap() function */
%option noyywrap
    /* we don't need yyunput() function */
%option nounput
    /* we don't need input() function */
%option noinput
    /* enable location */
%option bison-bridge
%option bison-locations

%{
#include "common/config.h"
#include "ast.h"
#include "yacc.tab.h"
#include "errors.h"
#include "common/datetime_utils.hpp"
#include <iostream>
#include <string>

// automatically update location
#define YY_USER_ACTION \
    yylloc->first_line = yylloc->last_line; \
    yylloc->first_column = yylloc->last_column; \
    for (int i = 0; yytext[i] != '\0'; i++) { \
        if(yytext[i] == '\n') { \
            yylloc->last_line++; \
            yylloc->last_column = 1; \
        } else { \
            yylloc->last_column++; \
        } \
    }

%}

alpha [a-zA-Z]
digit [0-9]
white_space [ \t]+
new_line "\r"|"\n"|"\r\n"
sign "+"|"-"
identifier {alpha}(_|{alpha}|{digit})*
value_int {sign}?{digit}+
value_float {sign}?{digit}+\.({digit}+)?
value_datetime '{digit}{4}-{digit}{2}-{digit}{2}{white_space}{digit}{2}:{digit}{2}:{digit}{2}'
value_string '[^']*'
single_op ";"|"("|")"|","|"*"|"="|">"|"<"|"."|"/"|"+"

%x STATE_COMMENT

%%
    /* block comment */
"/*" { BEGIN(STATE_COMMENT); }
<STATE_COMMENT>"*/" { BEGIN(INITIAL); }
<STATE_COMMENT>[^*] { /* ignore the text of the comment */ }
<STATE_COMMENT>\* { /* ignore *'s that aren't part of */ }
    /* single line comment */
"--".* { /* ignore single line comment */ }
    /* white space and new line */
{white_space} { /* ignore white space */ }
{new_line} { /* ignore new line */ }

"set output_file off" { output2file = false; }
    /* keywords */
"LOAD" { return LOAD; }
"SHOW" { return SHOW; }
"BEGIN" { return TXN_BEGIN; }
"COMMIT" { return TXN_COMMIT; }
"ABORT" { return TXN_ABORT; }
"ROLLBACK" { return TXN_ROLLBACK; }
"TABLES" { return TABLES; }
"CREATE" { return CREATE; }
"TABLE" { return TABLE; }
"DROP" { return DROP; }
"DESC" { return DESC; }
"INSERT" { return INSERT; }
"INTO" { return INTO; }
"VALUES" { return VALUES; }
"DELETE" { return DELETE; }
"FROM" { return FROM; }
"WHERE" { return WHERE; }
"UPDATE" { return UPDATE; }
"SET" { return SET; }
"SELECT" { return SELECT; }
"INT" { return INT; }
"BIGINT" { return BIGINT; }
"DATETIME" { return DATETIME; }
"CHAR" { return CHAR; }
"FLOAT" { return FLOAT; }
"INDEX" { return INDEX; }
"AND" { return AND; }
"JOIN" {return JOIN;}
"EXIT" { return EXIT; }
"HELP" { return HELP; }
"ORDER" { return ORDER; }
"BY" {  return BY;  }
"ASC" { return ASC; }
    /* Aggregate functions */
"COUNT" { return COUNT; }
"MAX"   { return MAX; }
"MIN"   { return MIN; }
"SUM"   { return SUM; }
"AS"    { return AS; }
"LIMIT" { return LIMIT; }
    /* operators */
">=" { return GEQ; }
"<=" { return LEQ; }
"<>" { return NEQ; }
{single_op} { return yytext[0]; }
    /* id */
{identifier} {
    yylval->sv_str = yytext;
    return IDENTIFIER;
}
    /* literals */
{value_int} {
    static constexpr char int_max[] = "2147483647",
                          int_min[] = "2147483648";
    const size_t num_len = strlen(yytext);
    // 返回 int
    auto is_int = [&] () mutable {
        yylval->sv_int = atoi(yytext);
        return VALUE_INT;
    };
    // 返回 bigint
    auto is_bigint = [&] () mutable {
        yylval->sv_bigint = std::stoll(yytext);
        return VALUE_BIGINT;
    };

    // 对于负数输入
    if (yytext[0] == '-') {
        if (num_len - 1 < sizeof(int_min)) {
            // yytext 长度小于 int_min 最小值长度 说明为 int 类型
            return is_int();
        } else if (num_len - 1 > sizeof(int_min)) {
            // yytext 长度大于 int_min 最小值长度 说明为 bigint 类型
            return is_bigint();
        } else {
            // 否则需要比较 yytext 于 int_min 的字典序
            if (strcmp(yytext + 1, int_min) <= 0)
                return is_int();
            else
                return is_bigint();
        }
    }
    // 对于有正号的正数输入
    else if (yytext[0] == '+') {
        if (num_len - 1 < sizeof(int_max)) {
            // yytext 长度小于 int_max 最小值长度 说明为 int 类型
            return is_int();
        } else if (num_len - 1 > sizeof(int_max)) {
            // yytext 长度大于 int_min 最小值长度 说明为 bigint 类型
            return is_bigint();
        } else {
            // 否则需要比较 yytext 于 int_min 的字典序
            if (strcmp(yytext + 1, int_max) <= 0)
                return is_int();
            else
                return is_bigint();
        }
    }
    // 对于有正号的正数输入
    else {
        if (num_len < sizeof(int_max)) {
            // yytext 长度小于 int_max 最小值长度 说明为 int 类型
            return is_int();
        } else if (num_len > sizeof(int_max)) {
            // yytext 长度大于 int_max 最小值长度 说明为 bigint 类型
            return is_bigint();
        } else {
            // 否则需要比较 yytext 于 int_min 的字典序
            if (strcmp(yytext, int_max) <= 0)
                return is_int();
            else
                return is_bigint();
        }
    }
}
{value_float} {
    yylval->sv_float = atof(yytext);
    return VALUE_FLOAT;
}
{value_datetime} {
    yylval->sv_datetime = datetime::to_bcd(std::string(yytext + 1, strlen(yytext) - 2));
    return VALUE_DATETIME;
}
{value_string} {
    yylval->sv_str = std::string(yytext + 1, strlen(yytext) - 2);
    return VALUE_STRING;
}
    /* EOF */
<<EOF>> { return T_EOF; }
    /* unexpected char */
. { std::cerr << "Lexer Error: unexpected character " << yytext[0] << std::endl; }
%%
