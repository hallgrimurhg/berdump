
for i in tests/*.in; do
	f=$(echo $i | sed "s/.in$//g");
	./berdump $f.in > $f.tmp
	cmp -s $f.out $f.tmp
	if [ $? -eq 0 ]; then
		echo "$f: OK"
		rm -f $f.tmp
	else
		echo "$f: FAILURE, $f.out and $f.tmp differ"
	fi
done

