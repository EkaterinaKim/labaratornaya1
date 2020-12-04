all:
	gcc -fPIC -c plugin_macaddr.c
	gcc -shared -o libmacaddr.so plugin_macaddr.o
	gcc funcs.h plugin_api.h main.c -o lab1 -ldl
