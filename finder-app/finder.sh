#!/bin/sh
# Check input arguments
if [ $# -lt 2 ]
then
	echo file directory or search string missing.
	echo "Usage: $0 /filesdir searchstr"

	exit 1
else
	FILESDIR=$1
	SEARCHSTR=$2
fi

# File directory not found
if [ ! -d $FILESDIR ]
then
	echo File directory not found.
	
	exit 1
fi

NUMFILES=$( ls $FILESDIR | wc -l )
MATCHLINES=$( grep -r "$SEARCHSTR" $FILESDIR | wc -l )
echo "The number of files are $NUMFILES and the number of matching lines are $MATCHLINES."
