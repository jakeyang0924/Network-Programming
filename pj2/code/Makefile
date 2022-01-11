all: np_simple np_single_proc np_multi_proc

np_simple: np_simple.c
	gcc -o np_simple np_simple.c
np_single_proc: np_single_proc.c
	gcc -o np_single_proc np_single_proc.c
np_multi_proc: np_multi_proc.c          
	gcc -o np_multi_proc np_multi_proc.c

clean:
	rm np_simple np_single_proc np_multi_proc
