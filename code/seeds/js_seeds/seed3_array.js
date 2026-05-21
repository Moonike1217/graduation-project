let arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
arr[10] = 11;
arr[11] = 12;
let sum = 0;
let i;
for (i = 0; i < 12; i = i + 1) {
    sum = sum + arr[i];
}
let x0 = arr[0];
let x5 = arr[5];
let x11 = arr[11];
print(sum);
print(x0);
print(x5);
print(x11);
