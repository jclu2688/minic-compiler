extern void print(int);
extern int read();

int main() {
    int x;
    int y;
    x = 5;
    y = x + 10;
    if (x < y) {
        print(y);
    }
    return x;
}
