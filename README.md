We have prepared a simple demo containing a list file writer and usage of pprint to read the file

    # build things
    ./configure
    cd build
    make -j pprint points

    # generate random points and display them
    TMP=`mktemp`; ./points > $TMP && ./pprint $TMP

    # generate random points and display only those within the [0,0,3,3] rectangle
    TMP=`mktemp`; ./points > $TMP && ./pprint $TMP -where '0 <= x && x <= 3 && 0 <= y && y <= 3'
