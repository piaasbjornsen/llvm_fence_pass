# llvm-pass-skeleton

A completely useless LLVM pass.
It's for LLVM 17.

Build:

    $ cd llvm-pass-skeleton
    $ mkdir build
    $ cd build
    $ cmake ..
    $ make
    $ cd ..

Run:

    $ clang -fpass-plugin=`echo build/skeleton/SkeletonPass.*` something.c

Run .c:

    $ gcc example.c -o program
    $ ./program
    
Run 

    $ `brew --prefix llvm`/bin/clang -fpass-plugin=`echo build/skeleton/SkeletonPass.*` programs/example.c

    $ `brew --prefix llvm`/bin/clang -S -emit-llvm programs/example.c -o ir/example.ll  
            
    $ `brew --prefix llvm`/bin/opt -load-pass-plugin=build/skeleton/SkeletonPass.dylib -passes="enforce-tso" -S ir/example.ll -o output/example.opt.ll

    $ `brew --prefix llvm`/bin/opt -load-pass-plugin=build/skeleton/SkeletonPass.dylib -S ir/example.ll -o output/example.opt.ll

