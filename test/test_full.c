int add(int x, int y) {
    return x + y;
}

int main() {
    int a;
    int b = 42;
    a = add(b, 10);

    if (a > 50) {
        return 1;
    } else {
        return 0;
    }
}
