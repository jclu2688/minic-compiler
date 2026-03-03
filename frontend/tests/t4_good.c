extern void print(int);
extern int read();

int main() {
    int x;
    x = 10;
    {
        int x;
        x = 20;
        print(x);
    }
    print(x);
    return x;
}
