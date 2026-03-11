extern void print(int);
extern int read();

int func(int n){
	int sum;
	int i;
	sum = 0;
	i = 1;
	while (i <= n){
		sum = sum + i;
		i = i + 1;
	}
	print(sum);
	return sum;
}
