#!/bin/bash -e
##==================================================================================
## MIT License
##
## Copyright (c) 2019 Quentin Quadrat <lecrapouille@gmail.com>
##
## Permission is hereby granted, free of charge, to any person obtaining a copy
## of this software and associated documentation files (the "Software"), to deal
## in the Software without restriction, including without limitation the rights
## to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
## copies of the Software, and to permit persons to whom the Software is
## furnished to do so, subject to the following conditions:
##
## The above copyright notice and this permission notice shall be included in all
## copies or substantial portions of the Software.
##
## THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
## IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
## FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
## AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
## LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
## OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
## SOFTWARE.
##==================================================================================

# This script, called by Makefile, makes a backup of the code source as
# an unique tarball. Tarballs name are unique <date>-<name>-v<version>-<counter>.tar.gz
# where $DATE is the date of today and $NTH a counter to make unique if
# a tarball already exists. Some informations are not backuped like .git
# generated doc, builds ...

# $1 is the content of pwd where to exectis script.
# $2 is the tarball name

#set -x

HERE=${1##*/}
NAME="$2"
THIRDPART="$3"
DATE=`date +%Y-%m-%d`
VERSION="$4"
NTH=0
TARGET_TGZ="${DATE}-${NAME}-v${VERSION}_`printf "%02d" ${NTH}`.tar.gz"

### Iterate for finding an unique name
while [ -e "${TARGET_TGZ}" ];
do
    NTH=`echo "${TARGET_TGZ}" | cut -d"v" -f2 | cut -d"." -f1`
    if [ "$NTH" == "" ];
    then
        echo "$0 ERROR: cannot manage this case ${TARGET_TGZ}"
        exit 1
    else
        NTH=$(( NTH + 1 ))
        TARGET_TGZ="${DATE}-${NAME}-v${VERSION}_`printf "%02d" ${NTH}`.tar.gz"
    fi
done

### Display informations with the same look than Makefile
echo -e "\033[35m*** Tarball:\033[00m \033[36m""${TARGET_TGZ}""\033[00m <= \033[33m$1\033[00m"

### Compress ../$NAME in /tmp, append the version number to the name and move the
### created tarball from /tmp into the NAME root directory.
EXTERNAL_FOLDERS=
if [ -d ${THIRDPART} ]; then
for i in `ls -d ${THIRDPART}/*/`
  do
    EXTERNAL_FOLDERS="--exclude=\"${i}\" ${EXTERNAL_FOLDERS}"
  done
fi
(cd .. && tar --transform s/${NAME}/${NAME}${VERSION}/ \
              --exclude='.git' --exclude="*-${NAME}-*.tar.gz" --exclude="doc/html" \
              --exclude "doc/coverage" --exclude "*/build" --exclude=".cache" \
              --exclude="exapkgs" ${EXTERNAL_FOLDERS} --exclude="latest.tar.gz" \
              -czvf /tmp/${TARGET_TGZ} $HERE \
              > /dev/null && mv /tmp/${TARGET_TGZ} $HERE)

ln -nsf ${TARGET_TGZ} latest.tar.gz
