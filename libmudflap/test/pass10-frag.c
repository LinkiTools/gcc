int foo[10];
int sz = sizeof (int);

char *bar = (char *)foo;
bar [sz * 9] = 0;
