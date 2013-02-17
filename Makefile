
micro-manager: micro-manager.c
	gcc -Wall -o micro-manager micro-manager.c

clean:
	rm -f micro-manager *.o *.bak *~

