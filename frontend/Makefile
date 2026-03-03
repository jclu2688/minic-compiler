# Makefile for MiniC Compiler

CXX = g++
CXXFLAGS = -std=c++11 -Wall -g

LEX = flex
YACC = bison

TARGET = frontend

SRCS = ast.c semantic.cpp main.cpp
LEX_SRC = minic.l
YACC_SRC = minic.y

LEX_OUT = lex.yy.c
YACC_OUT = minic.tab.c
YACC_HDR = minic.tab.h

all: $(TARGET)

$(TARGET): $(LEX_OUT) $(YACC_OUT) $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(YACC_OUT) $(LEX_OUT) $(SRCS)

$(LEX_OUT): $(LEX_SRC) $(YACC_HDR)
	$(LEX) $(LEX_SRC)

$(YACC_OUT) $(YACC_HDR): $(YACC_SRC)
	$(YACC) -d -o $(YACC_OUT) $(YACC_SRC)

clean:
	rm -f $(TARGET) $(LEX_OUT) $(YACC_OUT) $(YACC_HDR)

.PHONY: all clean
