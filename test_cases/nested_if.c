extern void print(int);
extern int read();

int func(int n){
	int result;
	result = 0;
	if (n > 10){
		if (n > 20){
			result = 3;
		}
		else {
			result = 2;
		}
	}
	else {
		result = 1;
	}
	print(result);
	return result;
}
