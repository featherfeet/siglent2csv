siglent2csv: siglent2csv.c siglent2csv.h
	gcc -Ofast -Wall -Wpedantic -o siglent2csv siglent2csv.c -lpthread
debug: siglent2csv.c siglent2csv.h
	gcc -g -O0 -Wall -Wpedantic -o siglent2csv siglent2csv.c -lpthread
asan: siglent2csv.c siglent2csv.h
	gcc -g -O0 -Wall -Wpedantic -fsanitize=address,undefined -o siglent2csv siglent2csv.c -lpthread
run: siglent2csv
	./siglent2csv usr_wf_data.bin csv_data.csv
clean:
	rm siglent2csv
