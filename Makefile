siglent2csv: siglent2csv.c siglent2csv.h
	gcc -Ofast -Wall -Wpedantic -o siglent2csv siglent2csv.c
debug: siglent2csv.c siglent2csv.h
	gcc -g -O0 -Wall -Wpedantic -o siglent2csv siglent2csv.c
asan: siglent2csv.c siglent2csv.h
	gcc -g -O0 -Wall -Wpedantic -fsanitize=address,undefined -o siglent2csv siglent2csv.c
run: siglent2csv
	./siglent2csv usr_wf_data.bin csv_data.csv
clean:
	rm siglent2csv
