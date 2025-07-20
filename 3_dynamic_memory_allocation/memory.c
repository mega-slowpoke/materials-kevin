#include <stdio.h>
#include <stdlib.h>


int global_var = 20;


int main() {    


    // malloc
    int n = 5;
    int *arr = (int *)  malloc(n * sizeof(int));

    // double* d_arr = (double *) malloc(n * sizeof(double));

    if (arr == NULL) {
        printf("Memory allocation failed!\n");
        return 1;
    }

    // memset(arr, 0, n * sizeof(int));

    for (int i = 0; i < n; i++) {
        arr[i] = i + 1;
        printf("%d ", arr[i]);
    }

    // ....

    free(arr);


    // calloc
    int n = 5;
    int *arr = (int *)calloc(n, sizeof(int));

    if (arr == NULL) {
        printf("Memory allocation failed!\n");
        return 1;
    }

    for (int i = 0; i < n; i++) {
        printf("%d ", arr[i]);  // 全部输出0
    }

    free(arr);




    // realloc
    int n = 3;
    int *arr = (int *)malloc(n * sizeof(int));

    if (arr == NULL) return 1;

    for (int i = 0; i < n; i++) arr[i] = i + 1;

    // 扩容到6个元素
    int *temp = (int *)realloc(arr, 6 * sizeof(int));
    if (temp == NULL) {
        free(arr);
        printf("Reallocation failed!\n");
        return 1;
    }
    arr = temp;

    for (int i = 3; i < 6; i++) arr[i] = (i + 1) * 10;

    for (int i = 0; i < 6; i++) {
        printf("%d ", arr[i]);
    }

    free(arr);



    return 0;

}
