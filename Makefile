.PHONY: build
build:
	gcc -o tinybasic main.c -O2 -g

.PHONY: clean
clean:
	-rm tinybasic