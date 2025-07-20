#include <stdio.h>
#include <stdlib.h>

int add(int x, int y) {
    int temp = 10;
    return x+ y;
}
int main() {
    int integer = 10;
    double double_var = 3.14;
    int int_var = (int) double_var;
    add(10,10);
    printf("%d\n", int_var);


    double_var = (double) integer;
    printf("%f\n", double_var);
}