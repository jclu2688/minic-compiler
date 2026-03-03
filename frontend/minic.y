%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "ast.h"

extern int yylex();
extern int yylineno;
extern FILE *yyin;

void yyerror(const char *s);

astNode *root = NULL;
%}

%union {
    int ival;
    char *sval;
    astNode *node;
    std::vector<astNode*> *vec;
    rop_type rop;
    op_type op;
}

%token <ival> NUM
%token <sval> ID
%token EXTERN INT VOID IF ELSE WHILE RETURN PRINT READ
%token ASSIGN PLUS MINUS MULT DIV
%token EQ NEQ LT GT LTE GTE
%token SEMI COMMA LPAREN RPAREN LBRACE RBRACE

%type <node> program extern_print extern_read func_decl param block
%type <node> stmt decl_stmt if_stmt while_stmt return_stmt assign_stmt call_stmt
%type <node> expr rel_expr add_expr mul_expr unary_expr primary_expr var
%type <vec> stmt_list

/* Precedence (lowest to highest) */
%right ASSIGN
%left EQ NEQ
%left LT GT LTE GTE
%left PLUS MINUS
%left MULT DIV
%right UMINUS

%%

program:
    extern_print extern_read func_decl { 
        root = createProg($1, $2, $3); 
    }
    ;

extern_print:
    EXTERN VOID PRINT LPAREN INT RPAREN SEMI {
        $$ = createExtern("print");
    }
    ;

extern_read:
    EXTERN INT READ LPAREN RPAREN SEMI {
        $$ = createExtern("read");
    }
    ;

func_decl:
    INT ID LPAREN param RPAREN block {
        $$ = createFunc($2, $4, $6);
        free($2);
    }
    | INT ID LPAREN RPAREN block {
        $$ = createFunc($2, NULL, $5);
        free($2);
    }
    ;

param:
    INT ID {
        $$ = createVar($2);
        free($2);
    }
    ;

block:
    LBRACE stmt_list RBRACE {
        $$ = createBlock($2);
    }
    | LBRACE RBRACE {
        $$ = createBlock(new std::vector<astNode*>());
    }
    ;

stmt_list:
    stmt_list stmt {
        $1->push_back($2);
        $$ = $1;
    }
    | stmt {
        $$ = new std::vector<astNode*>();
        $$->push_back($1);
    }
    ;

stmt:
    decl_stmt { $$ = $1; }
    | if_stmt { $$ = $1; }
    | while_stmt { $$ = $1; }
    | return_stmt { $$ = $1; }
    | assign_stmt { $$ = $1; }
    | call_stmt SEMI { $$ = $1; }
    | block { $$ = $1; }
    ;

decl_stmt:
    INT ID SEMI {
        $$ = createDecl($2);
        free($2);
    }
    ;

if_stmt:
    IF LPAREN expr RPAREN stmt {
        $$ = createIf($3, $5, NULL);
    }
    | IF LPAREN expr RPAREN stmt ELSE stmt {
        $$ = createIf($3, $5, $7);
    }
    ;

while_stmt:
    WHILE LPAREN expr RPAREN stmt {
        $$ = createWhile($3, $5);
    }
    ;

return_stmt:
    RETURN expr SEMI {
        $$ = createRet($2);
    }
    ;

assign_stmt:
    var ASSIGN expr SEMI {
        $$ = createAsgn($1, $3);
    }
    ;

call_stmt:
    PRINT LPAREN expr RPAREN {
        $$ = createCall("print", $3);
    }
    | READ LPAREN RPAREN {
        $$ = createCall("read", NULL);
    }
    ;

var:
    ID {
        $$ = createVar($1);
        free($1);
    }
    ;

expr:
    rel_expr { $$ = $1; }
    ;

rel_expr:
    rel_expr EQ add_expr  { $$ = createRExpr($1, $3, eq); }
    | rel_expr NEQ add_expr { $$ = createRExpr($1, $3, neq); }
    | rel_expr LT add_expr  { $$ = createRExpr($1, $3, lt); }
    | rel_expr GT add_expr  { $$ = createRExpr($1, $3, gt); }
    | rel_expr LTE add_expr { $$ = createRExpr($1, $3, le); }
    | rel_expr GTE add_expr { $$ = createRExpr($1, $3, ge); }
    | add_expr { $$ = $1; }
    ;

add_expr:
    add_expr PLUS mul_expr  { $$ = createBExpr($1, $3, add); }
    | add_expr MINUS mul_expr { $$ = createBExpr($1, $3, sub); }
    | mul_expr { $$ = $1; }
    ;

mul_expr:
    mul_expr MULT unary_expr  { $$ = createBExpr($1, $3, mul); }
    | mul_expr DIV unary_expr { $$ = createBExpr($1, $3, divide); }
    | unary_expr { $$ = $1; }
    ;

unary_expr:
    MINUS unary_expr %prec UMINUS {
        $$ = createUExpr($2, uminus);
    }
    | primary_expr { $$ = $1; }
    ;

primary_expr:
    NUM { $$ = createCnst($1); }
    | var { $$ = $1; }
    | LPAREN expr RPAREN { $$ = $2; }
    | READ LPAREN RPAREN { $$ = createCall("read", NULL); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Parse error at line %d: %s\n", yylineno, s);
}
