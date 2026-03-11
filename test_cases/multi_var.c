extern void print(int);
extern int read();

int func(int p){
	int a;
	int b;
	int c;
	int d;
	int e;
	a = p + 1;
	b = a + 2;
	c = b + 3;
	d = c + 4;
	e = d + 5;
	print(a);
	print(b);
	print(c);
	print(d);
	print(e);
	return e;
}
