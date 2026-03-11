extern void print(int);
extern int read();

int func(int p){
	int a;
	int b;
	int c;
	a = 10;
	b = 3;
	c = a + b;
	print(c);
	c = a - b;
	print(c);
	c = a * b;
	print(c);
	c = a / b;
	print(c);
	return c;
}
