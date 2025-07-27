#include <stdio.h>
#include <stdlib.h>

int main() {
    return 1;
}

int factorial(int n) {
    int res = n;
   for (int i = n-1; i > 0; i--) {
      res *= i;
   }
   return res;
}

int factorialRec(int n) {
    // base case
    if (n == 0) {
        return 1;
    }

    // recursive step
    return  n * factorialRec(n-1);

}