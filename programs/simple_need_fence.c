#include <stdio.h>

int main() {
    int x = 10;
    int y;

    // Memory access instructions
    y = x;      // Load instruction
    x = y + 1;  // Store instruction

    printf("y = %d\n", y);
    printf("x = %d\n", x);

    return 0;
}
