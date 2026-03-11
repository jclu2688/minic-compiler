extern void print(int);
extern int read();

int func(int n){
	int result;
	if (n > 0){
		result = 1;
	}
	else {
		result = 0;
	}
	print(result);
	return result;
}
