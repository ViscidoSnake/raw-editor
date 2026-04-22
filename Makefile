raw-editor.exe: raw-editor.c
	$(CC) raw-editor.c -o raw-editor.exe -Wall -Wextra -pedantic -std=c99
