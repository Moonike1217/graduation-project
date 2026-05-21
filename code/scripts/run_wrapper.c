/* run_wrapper.c
   AFL passes @@ as a file path; test programs need argv[1] as an integer.
   Usage: run_wrapper <target_binary> <input_file>
   Reads the first integer from input_file, exec's target_binary with it.
*/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <binary> <input_file>\n", argv[0]);
        return 1;
    }
    char *binary = argv[1];
    char *input_file = argv[2];

    FILE *fp = fopen(input_file, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", input_file);
        return 1;
    }
    int val;
    if (fscanf(fp, "%d", &val) != 1) {
        fprintf(stderr, "Cannot read integer from %s\n", input_file);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    char val_str[32];
    snprintf(val_str, sizeof(val_str), "%d", val);

    char *new_argv[] = {binary, val_str, NULL};
    execvp(binary, new_argv);

    perror("execvp failed");
    return 1;
}
