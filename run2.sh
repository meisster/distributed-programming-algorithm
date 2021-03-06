#!/bin/bash

HOST="unixlab.cs.put.poznan.pl"
USER="inf136705"
WORK_DIRECTORY="mpi/szafa/"
FILE_NAME=$1
NUMBER_OF_HOSTS="${2:-17}"
OPT_VAR=$3

function connect () {
    ssh -T $USER@$HOST << END
        $(typeset -f compileAndRun)
        compileAndRun $NUMBER_OF_HOSTS $FILE_NAME $WORK_DIRECTORY $OPT_VAR 
END
}

function compileAndRun () {
    echo "Łączę się do lab-os-12..."
    ssh -T lab-os-12 << ENDCOMPILE
        echo "Liczba hostów: $1; Nazwa pliku: $2; Dodatkowe parametry: $4"
        echo "#####################################################"
        cd $3
        mpic++ $2 -o algorytm
        mpirun -hostfile hosts -np $1 algorytm $3
ENDCOMPILE
}

if [ ! -z "$FILE_NAME" ]; then
    echo "Kopiuje plik $FILE_NAME do unixlab..."
    scp $FILE_NAME $USER@$HOST:~/$WORK_DIRECTORY
    if [ "$?" -eq "0" ];
        then 
            connect
            echo "#####################################################"
        else
            echo "Nie udalo sie skopiowac pliku."
        fi
else 
    echo "Podaj nazwę pliku do skopiowania i uruchomienia."
fi