CXX = g++
CXXFLAGS = -std=c++11 -Wall -g `llvm-config-17 --cxxflags` -I../frontend
LDFLAGS = `llvm-config-17 --ldflags --libs core` -lpthread -lncurses -ldl

LEX = flex
YACC = bison

TARGET = llvm_builder
SRCS = main.cpp llvm_builder.cpp ../frontend/ast.c ../frontend/lex.yy.c ../frontend/minic.tab.c
OBJS = main.o llvm_builder.o ast.o lex.yy.o minic.tab.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

ast.o: ../frontend/ast.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

minic.tab.o: ../frontend/minic.tab.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

lex.yy.o: ../frontend/lex.yy.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

../frontend/minic.tab.c ../frontend/minic.tab.h ../frontend/lex.yy.c:
	make -C ../frontend minic.tab.c lex.yy.c

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) *.o ../frontend/*.o

.PHONY: all clean
