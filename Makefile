CXX = g++
CXXFLAGS = -std=c++11 -Wall -g `llvm-config-17 --cxxflags` -I. -Ifrontend -Illvm_builder -DMAIN_UNIFIED
LDFLAGS = `llvm-config-17 --ldflags --libs core irreader` -lpthread -lncurses -ldl
CC = gcc
CFLAGS = -Wall -g `llvm-config-17 --cflags` -I. -Ifrontend -Illvm_builder

TARGET = minic
SRCS = main.cpp \
       llvm_builder/llvm_builder.cpp \
       optimization/optimizer.c \
       frontend/ast.c \
       frontend/semantic.cpp \
       frontend/lex.yy.c \
       frontend/minic.tab.c

OBJS = $(SRCS:.cpp=.o)
OBJS := $(OBJS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Rule to reuse the parser/lexer from frontend
frontend/minic.tab.c frontend/minic.tab.h: frontend/Makefile
	make -C frontend minic.tab.c

frontend/lex.yy.c: frontend/Makefile
	make -C frontend lex.yy.c

frontend/ast.o: frontend/ast.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) *.o llvm_builder/*.o optimization/*.o frontend/*.o
	make -C llvm_builder clean

.PHONY: all clean
