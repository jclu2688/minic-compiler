extern void print(int);
extern int read();

int func(int n){
	int x;
	print(n);
	x = n + 5;
	print(x);
	return x;
}
