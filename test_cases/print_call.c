extern void print(int);
extern int read();

int func(int n){
	int i;
	i = 0;
	while (i < n){
		print(i);
		i = i + 1;
	}
	return n;
}
