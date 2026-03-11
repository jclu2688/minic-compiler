extern void print(int);
extern int read();

int func(int n){
	int result;
	if (n > 0)
		result = n + 10;
	else
		result = n - 10;
	return result;
}
