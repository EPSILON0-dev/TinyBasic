.PHONY: build
build:
	gcc -o tinybasic main.c -O0 -g

.PHONY: clean
clean:
	-rm tinybasic