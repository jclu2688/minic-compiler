extern void print(int);
extern int read();

int func(int n){
	int a;
	int b;
	int temp;
	int i;
	a = 0;
	b = 1;
	i = 0;
	while (i < n){
		print(a);
		temp = b;
		b = a + b;
		a = temp;
		i = i + 1;
	}
	return a;
}
