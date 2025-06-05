#!/bin/sh

# make fresh virtual disk
./fs_make.x disk.fs 4096 > /dev/null 2>&1

# get fs_info from reference lib
./fs_ref.x info disk.fs >ref.stdout 2>ref.stderr

# get fs_info from my lib
./test_fs.x info disk.fs >lib.stdout 2>lib.stderr

# put output files into variables
REF_STDOUT=$(cat ref.stdout)
REF_STDERR=$(cat ref.stderr)

LIB_STDOUT=$(cat lib.stdout)
LIB_STDERR=$(cat lib.stderr)

# compare stdout
if [ "$REF_STDOUT" != "$LIB_STDOUT" ]; then
    echo "Stdout outputs don't match..."
    diff -u ref.stdout lib.stdout
else
    echo "Stdout outputs match!"
fi

# compare stderr
if [ "$REF_STDERR" != "$LIB_STDERR" ]; then
    echo "Stderr outputs don't match..."
    diff -u ref.stderr lib.stderr
else
    echo "Stderr outputs match!"
fi

# clean
rm disk.fs
rm ref.stdout ref.stderr
rm lib.stdout lib.stderr

# fs_ls testing
./fs_make.x empty.fs 8192 > /dev/null 2>&1

./fs_make.x add_1.fs 2048 > /dev/null 2>&1
./fs_ref.x add add_1.fs simple_reader.c > /dev/null 2>&1

./fs_make.x add_2.fs 4096 > /dev/null 2>&1
./fs_ref.x add add_2.fs simple_reader.c > /dev/null 2>&1
./fs_ref.x add add_2.fs test_fs.c > /dev/null 2>&1

./fs_make.x add_3_rm_1.fs 7420 > /dev/null 2>&1
./fs_ref.x add add_3_rm_1.fs simple_reader.c > /dev/null 2>&1
./fs_ref.x add add_3_rm_1.fs test_fs.c > /dev/null 2>&1
./fs_ref.x add add_3_rm_1.fs Makefile > /dev/null 2>&1
./fs_ref.x rm add_3_rm_1.fs test_fs.c > /dev/null 2>&1

for fs in *.fs; do
    echo "Running ls diff $fs"
    ./fs_ref.x ls "$fs" > ref_output
    ./test_fs.x ls "$fs" > my_output
    diff ref_output my_output
done

# clean
rm empty.fs add_1.fs add_2.fs add_3_rm_1.fs
rm ref_output my_output

# comparing outputs of scripts
for script in scripts/*.script; do
    echo "Running $script"
    dd if=/dev/urandom of=test_file bs=4096 count=1 > /dev/null 2>&1
    ./fs_make.x test.fs 100 > /dev/null 2>&1
    ./test_fs.x script test.fs $script > my_script_ouput 2> my_script_err
    ./test_fs.x info test.fs > my_output_info
    ./test_fs.x ls test.fs > my_output_ls
    rm test_file test.fs

    dd if=/dev/urandom of=test_file bs=4096 count=1 > /dev/null 2>&1
    ./fs_make.x test.fs 100 > /dev/null 2>&1
    ./fs_ref.x script test.fs $script > ref_script_ouput 2> ref_script_err
    ./fs_ref.x info test.fs > ref_output_info
    ./fs_ref.x ls test.fs > ref_output_ls
    rm test_file test.fs


    if [ "$(cat my_script_ouput)" != "$(cat ref_script_ouput)" ]; then
        echo "Script outputs don't match..."
        diff -u my_script_ouput ref_script_ouput
    fi

    if [ "$(cat my_script_err)" != "$(cat ref_script_err)" ]; then
        echo "Script errors don't match..."
        diff -u my_script_err ref_script_err
    fi

    if [ "$(cat my_output_info)" != "$(cat ref_output_info)" ]; then
        echo "Disk info don't match..."
        diff -u my_output_info ref_output_info
    fi

    if [ "$(cat my_output_ls)" != "$(cat ref_output_ls)" ]; then
        echo "Disk ls don't match..."
        diff -u my_output_ls ref_output_ls
    fi

    rm my_script_ouput ref_script_ouput my_output_info ref_output_info my_output_ls ref_output_ls my_script_err ref_script_err
done
