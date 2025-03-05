#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(int argn, char *argv[]) {
FILE *f=fopen(argv[1],"r"), *g=fopen(argv[2],"w");
char *str = (char *) calloc(15,sizeof(char));
fscanf(f,"%s",str);
fprintf(g,"%s",str);
free(str);return 0; }
