#!/bin/sh
# Check input arguments
if [ $# -lt 2 ]
then
	echo file directory or write string missing.
	echo "Usage: $0 /path/filename writestr"

	exit 1
else
	WRITEFILE=$1
	WRITESTR=$2
fi

# Create directory
DIRNAME=$( dirname $WRITEFILE )
if [ ! -d $DIRNAME ]
then
	mkdir -p $DIRNAME
fi
# Directory create failed
if [ $? -eq 1 ]
then
	echo Directory cannot be created.

	exit 1
fi

# Create file
touch $WRITEFILE

# File create failed
if [ $? -eq 1 ]
then
	echo File cannot be created.
	
	exit 1
else
	echo "$WRITESTR" > $WRITEFILE
fi
