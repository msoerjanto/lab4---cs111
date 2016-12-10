.SILENT:

part2:
	gcc -lmraa -pthread -o lab4_part2 lab4_part2.c -lm
	./lab4_part2
part1:
	gcc -lmraa -o lab4_part1 lab4_part1.c -lm
	./lab4_part1
clean:
	rm lab4_part2
	rm lab4_part1
