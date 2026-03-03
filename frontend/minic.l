%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "minic.tab.h"

extern void yyerror(const char *s);
%}

%option noyywrap
%option yylineno

%%

[ \t\n\r]+      { /* ignore whitespace */ }

"extern"        { return EXTERN; }
"int"           { return INT; }
"void"          { return VOID; }
"if"            { return IF; }
"else"          { return ELSE; }
"while"         { return WHILE; }
"return"        { return RETURN; }
"print"         { return PRINT; }
"read"          { return READ; }

"+"             { return PLUS; }
"-"             { return MINUS; }
"*"             { return MULT; }
"/"             { return DIV; }
"="             { return ASSIGN; }
"=="            { return EQ; }
"!="            { return NEQ; }
"<="            { return LTE; }
">="            { return GTE; }
"<"             { return LT; }
">"             { return GT; }

";"             { return SEMI; }
","             { return COMMA; }
"("             { return LPAREN; }
")"             { return RPAREN; }
"{"             { return LBRACE; }
"}"             { return RBRACE; }

[0-9]+          { yylval.ival = atoi(yytext); return NUM; }
[a-zA-Z_][a-zA-Z0-9_]* { yylval.sval = strdup(yytext); return ID; }

.               { fprintf(stderr, "Lexer error: Unknown character '%s' at line %d\n", yytext, yylineno); }

%%
