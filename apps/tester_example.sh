#!/bin/sh

# make fresh virtual disk
./fs_make.x disk.fs 4096

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

# ls testing
./fs_make.x empty.fs 8192

./fs_make.x add_1.fs 2048
./fs_ref.x add add_1.fs simple_reader.c

./fs_make.x add_2.fs 4096
./fs_ref.x add add_2.fs simple_reader.c
./fs_ref.x add add_2.fs test_fs.c

./fs_make.x add_3_rm_1.fs 7420
./fs_ref.x add add_3_rm_1.fs simple_reader.c
./fs_ref.x add add_3_rm_1.fs test_fs.c
./fs_ref.x add add_3_rm_1.fs Makefile
./fs_ref.x rm add_3_rm_1.fs test_fs.c

for fs in *.fs; do
    echo "Running ls diff $fs"
    ./fs_ref.x ls "$fs" > ref_output
    ./test_fs.x ls "$fs" > my_output
    diff ref_output my_output
done

# clean
rm empty.fs add_1.fs add_2.fs add_3_rm_1.fs
rm ref_output my_output